#include "face_tracking/servo/servo_driver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>

namespace face_tracking::servo {
namespace {
int pulse_for(const ServoAxisSettings& axis, float angle) {
  float ratio = angle / axis.rated_max_deg;
  if (axis.invert) ratio = 1.0F - ratio;
  return static_cast<int>(std::lround(
      axis.min_pulse_us + ratio * static_cast<float>(axis.max_pulse_us - axis.min_pulse_us)));
}

std::int64_t now_unix_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}

struct ServoDriver::Implementation {
  Implementation(ServoDriverSettings value, ServoPwmPort& port)
      : settings(std::move(value)), pwm(port) {}

  ServoDriverSettings settings;
  ServoPwmPort& pwm;
  PanTiltCommandedState state;
  std::int64_t last_upstream_at_unix_ns{};
  std::optional<std::tuple<std::string, std::string, std::uint64_t>> last_command;
  bool started{};
  float output_pan_deg{};
  float output_tilt_deg{};
  std::int64_t last_advance_at_unix_ns{};

  void write_axis(const ServoAxisSettings& axis, float angle) {
    pwm.set_pulse(axis.gpio, pulse_for(axis, angle), settings.frequency_hz);
  }

  void update_time(std::int64_t now) {
    state.updated_at_unix_ns = now;
    state.last_error.reset();
  }

  void hold(ServoDecision decision, std::int64_t now) {
    state.state = ServoDriverState::holding;
    state.decision = decision;
    state.pan_limit_held = false;
    state.tilt_limit_held = false;
    update_time(now);
  }

  void home(ServoDecision decision, std::int64_t now) {
    write_axis(settings.pan, settings.pan.home_deg);
    write_axis(settings.tilt, settings.tilt.home_deg);
    output_pan_deg = settings.pan.home_deg;
    output_tilt_deg = settings.tilt.home_deg;
    state.commanded_pan_deg = settings.pan.home_deg;
    state.commanded_tilt_deg = settings.tilt.home_deg;
    state.state = ServoDriverState::holding;
    state.decision = decision;
    state.pan_limit_held = false;
    state.tilt_limit_held = false;
    state.pwm_active = true;
    update_time(now);
  }
};

ServoDriver::ServoDriver(ServoDriverSettings settings, ServoPwmPort& pwm)
    : implementation_(std::make_unique<Implementation>(std::move(settings), pwm)) {}
ServoDriver::~ServoDriver() = default;

const PanTiltCommandedState& ServoDriver::start(std::int64_t now) {
  auto& driver = *implementation_;
  driver.pwm.start(driver.settings.pwm_chip);
  driver.started = true;
  driver.last_upstream_at_unix_ns = now;
  driver.state.state = ServoDriverState::homing;
  driver.home(ServoDecision::home_startup, now);
  return driver.state;
}

const PanTiltCommandedState& ServoDriver::process(const PanTiltDelta& command, std::int64_t now) {
  auto& driver = *implementation_;
  if (!driver.started) throw std::logic_error("servo driver has not started");
  try {
    validate(command);
  } catch (const std::exception&) {
    ++driver.state.rejected_commands;
    driver.state.decision = ServoDecision::rejected_invalid;
    driver.state.state = ServoDriverState::holding;
    driver.update_time(now);
    return driver.state;
  }

  const auto command_age_ns = now - command.computed_at_unix_ns;
  if (command_age_ns < -50'000'000LL ||
      command_age_ns > static_cast<std::int64_t>(driver.settings.command_max_age_ms) * 1'000'000LL) {
    ++driver.state.rejected_commands;
    driver.hold(ServoDecision::held_stale, now);
    return driver.state;
  }
  if (command.reason == ControllerDecision::lost) {
    driver.last_upstream_at_unix_ns = now;
    driver.home(ServoDecision::home_lost, now);
    return driver.state;
  }
  if (command.reason == ControllerDecision::no_target) {
    driver.last_upstream_at_unix_ns = now;
    driver.home(ServoDecision::home_no_target, now);
    return driver.state;
  }
  if (command.reason == ControllerDecision::stale) {
    driver.hold(ServoDecision::held_stale, now);
    return driver.state;
  }
  if (std::abs(command.delta_pan_deg) > driver.settings.max_input_pan_delta_deg ||
      std::abs(command.delta_tilt_deg) > driver.settings.max_input_tilt_delta_deg) {
    ++driver.state.rejected_commands;
    driver.state.decision = ServoDecision::rejected_invalid;
    driver.state.state = ServoDriverState::holding;
    driver.update_time(now);
    return driver.state;
  }
  if (command.selected_track_id != 0) driver.state.last_track_id = command.selected_track_id;

  if (!command.source_instance_id.empty() && !command.tracker_instance_id.empty()) {
    const auto identity = std::tuple{command.source_instance_id, command.tracker_instance_id,
                                     command.sequence};
    if (driver.last_command && std::get<0>(identity) == std::get<0>(*driver.last_command) &&
        std::get<1>(identity) == std::get<1>(*driver.last_command)) {
      if (command.sequence < std::get<2>(*driver.last_command)) {
        ++driver.state.rejected_commands;
        driver.state.decision = ServoDecision::rejected_out_of_order;
        driver.state.state = ServoDriverState::holding;
        driver.update_time(now);
        return driver.state;
      }
      if (command.sequence == std::get<2>(*driver.last_command)) {
        ++driver.state.rejected_commands;
        driver.state.decision = ServoDecision::rejected_duplicate;
        driver.state.state = ServoDriverState::holding;
        driver.update_time(now);
        return driver.state;
      }
    }
    driver.last_command = identity;
  }

  switch (command.reason) {
    case ControllerDecision::applied: {
      driver.last_upstream_at_unix_ns = now;
      if (!driver.settings.tracking_enabled) {
        ++driver.state.rejected_commands;
        driver.state.decision = ServoDecision::rejected_invalid;
        driver.state.state = ServoDriverState::holding;
        driver.update_time(now);
        return driver.state;
      }
      const float next_pan = driver.state.commanded_pan_deg + command.delta_pan_deg;
      const float next_tilt = driver.state.commanded_tilt_deg + command.delta_tilt_deg;
      driver.state.pan_limit_held =
          next_pan < driver.settings.pan.min_deg || next_pan > driver.settings.pan.max_deg;
      driver.state.tilt_limit_held =
          next_tilt < driver.settings.tilt.min_deg || next_tilt > driver.settings.tilt.max_deg;
      bool changed = false;
      if (!driver.state.pan_limit_held && command.delta_pan_deg != 0) {
        driver.state.commanded_pan_deg = next_pan;
        changed = true;
      }
      if (!driver.state.tilt_limit_held && command.delta_tilt_deg != 0) {
        driver.state.commanded_tilt_deg = next_tilt;
        changed = true;
      }
      if (changed) ++driver.state.applied_commands;
      if (driver.state.pan_limit_held || driver.state.tilt_limit_held) {
        ++driver.state.rejected_commands;
        driver.state.decision = ServoDecision::held_limit;
      } else {
        driver.state.decision = ServoDecision::applied;
      }
      driver.state.state = ServoDriverState::tracking;
      driver.update_time(now);
      return driver.state;
    }
    case ControllerDecision::deadband:
      driver.last_upstream_at_unix_ns = now;
      driver.state.decision = ServoDecision::held_deadband;
      driver.state.state = ServoDriverState::holding;
      break;
    case ControllerDecision::missing_hold:
      driver.last_upstream_at_unix_ns = now;
      driver.state.decision = ServoDecision::held_missing;
      driver.state.state = ServoDriverState::holding;
      break;
    case ControllerDecision::lost:
    case ControllerDecision::no_target:
    case ControllerDecision::stale:
      throw std::logic_error("pre-ordering decisions must be handled before ordering checks");
    case ControllerDecision::duplicate:
      ++driver.state.rejected_commands;
      driver.state.decision = ServoDecision::rejected_duplicate;
      driver.state.state = ServoDriverState::holding;
      break;
    case ControllerDecision::out_of_order:
      ++driver.state.rejected_commands;
      driver.state.decision = ServoDecision::rejected_out_of_order;
      driver.state.state = ServoDriverState::holding;
      break;
  }
  driver.state.pan_limit_held = false;
  driver.state.tilt_limit_held = false;
  driver.update_time(now);
  return driver.state;
}

void ServoDriver::note_upstream_online(std::int64_t now) {
  implementation_->last_upstream_at_unix_ns = now;
}

const PanTiltCommandedState& ServoDriver::upstream_lost(std::int64_t now) {
  implementation_->home(ServoDecision::home_upstream_timeout, now);
  return implementation_->state;
}

bool ServoDriver::check_timeout(std::int64_t now) {
  auto& driver = *implementation_;
  const auto timeout_ns =
      static_cast<std::int64_t>(driver.settings.upstream_timeout_ms) * 1'000'000LL;
  if (!driver.started || now - driver.last_upstream_at_unix_ns < timeout_ns ||
      driver.state.decision == ServoDecision::home_upstream_timeout) {
    return false;
  }
  driver.home(ServoDecision::home_upstream_timeout, now);
  return true;
}

bool ServoDriver::advance(std::int64_t now) {
  auto& driver = *implementation_;
  if (!driver.started) return false;
  const double dt_ms = std::min(
      100.0, std::max(0.0, (now - driver.last_advance_at_unix_ns) / 1'000'000.0));
  driver.last_advance_at_unix_ns = now;
  if (dt_ms <= 0) return false;
  const auto step_axis = [&driver, dt_ms](const ServoAxisSettings& axis, float& current,
                                          float target, float max_velocity) {
    const float delta = target - current;
    if (delta == 0.0F) return false;
    const float max_step = max_velocity * static_cast<float>(dt_ms) / 1000.0F;
    const float step = std::clamp(delta, -max_step, max_step);
    if (std::abs(step) < 0.001F) return false;
    current += step;
    return true;
  };
  bool moved = false;
  if (step_axis(driver.settings.pan, driver.output_pan_deg,
                driver.state.commanded_pan_deg,
                driver.settings.pan_max_velocity_deg_per_s)) {
    driver.write_axis(driver.settings.pan, driver.output_pan_deg);
    moved = true;
  }
  if (step_axis(driver.settings.tilt, driver.output_tilt_deg,
                driver.state.commanded_tilt_deg,
                driver.settings.tilt_max_velocity_deg_per_s)) {
    driver.write_axis(driver.settings.tilt, driver.output_tilt_deg);
    moved = true;
  }
  if (moved) driver.update_time(now);
  return moved;
}

const PanTiltCommandedState& ServoDriver::stop(std::int64_t now) noexcept {
  auto& driver = *implementation_;
  driver.pwm.stop();
  driver.started = false;
  driver.state.updated_at_unix_ns = now;
  driver.state.state = ServoDriverState::stopped;
  driver.state.pwm_active = false;
  return driver.state;
}

const PanTiltCommandedState& ServoDriver::fail(std::string error, std::int64_t now) noexcept {
  auto& driver = *implementation_;
  driver.pwm.stop();
  driver.started = false;
  driver.state.updated_at_unix_ns = now;
  driver.state.state = ServoDriverState::error;
  driver.state.decision = ServoDecision::error;
  driver.state.pwm_active = false;
  driver.state.last_error = std::move(error);
  return driver.state;
}

const PanTiltCommandedState& ServoDriver::state() const { return implementation_->state; }

struct ServoDriverService::Implementation {
  Implementation(ServoDriverSettings settings, ServoPwmPort& pwm, TransportPort& port)
      : driver(std::move(settings), pwm), transport(port) {}

  ServoDriver driver;
  TransportPort& transport;
  std::mutex mutex;
  std::deque<std::variant<PanTiltDelta, UpstreamEvent>> events;
  bool upstream_online{};
};

ServoDriverService::ServoDriverService(
    ServoDriverSettings settings, ServoPwmPort& pwm, TransportPort& transport)
    : implementation_(std::make_unique<Implementation>(std::move(settings), pwm, transport)) {}
ServoDriverService::~ServoDriverService() = default;

void ServoDriverService::run(std::stop_token stop_token) {
  auto& service = *implementation_;
  try {
    service.transport.publish_state(service.driver.start(now_unix_ns()));
    service.transport.start(
        [&service](PanTiltDelta command) {
          std::lock_guard lock(service.mutex);
          service.events.emplace_back(std::move(command));
        },
        [&service](UpstreamEvent event) {
          std::lock_guard lock(service.mutex);
          service.events.emplace_back(event);
        });
    auto last_status_at = std::chrono::steady_clock::now();
    while (!stop_token.stop_requested()) {
      std::deque<std::variant<PanTiltDelta, UpstreamEvent>> events;
      {
        std::lock_guard lock(service.mutex);
        events.swap(service.events);
      }
      const auto now = now_unix_ns();
      for (const auto& event : events) {
        if (const auto* upstream = std::get_if<UpstreamEvent>(&event)) {
          if (*upstream == UpstreamEvent::online) {
            service.upstream_online = true;
            service.driver.note_upstream_online(now);
          } else if (*upstream == UpstreamEvent::offline) {
            service.upstream_online = false;
            service.transport.publish_state(service.driver.upstream_lost(now));
          }
          continue;
        }
        if (service.upstream_online) {
          service.transport.publish_state(
              service.driver.process(std::get<PanTiltDelta>(event), now));
        }
      }
      if (service.driver.check_timeout(now)) {
        service.transport.publish_state(service.driver.state());
      }
      if (service.driver.advance(now)) {
        service.transport.publish_state(service.driver.state());
      }
      const auto steady_now = std::chrono::steady_clock::now();
      if (steady_now - last_status_at >= std::chrono::milliseconds(250)) {
        service.transport.publish_state(service.driver.state());
        last_status_at = steady_now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    service.transport.stop();
    service.transport.publish_state(service.driver.stop(now_unix_ns()));
  } catch (const std::exception& error) {
    try {
      service.transport.publish_state(service.driver.fail(error.what(), now_unix_ns()));
    } catch (const std::exception&) {
    }
    service.transport.stop();
    throw;
  }
}

}  // namespace face_tracking::servo
