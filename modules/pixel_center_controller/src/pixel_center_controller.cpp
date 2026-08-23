#include "face_tracking/controller/pixel_center_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

namespace face_tracking::controller {
namespace {
PanTiltDelta zero_command(
    const SelectedTargetObservation& observation, std::int64_t now, ControllerDecision reason) {
  return {
      .source_instance_id = observation.source_instance_id,
      .tracker_instance_id = observation.tracker_instance_id,
      .sequence = observation.sequence,
      .captured_at_unix_ns = observation.captured_at_unix_ns,
      .computed_at_unix_ns = now,
      .selected_track_id = observation.selected_track_id,
      .reason = reason,
  };
}
}  // namespace

struct PixelCenterController::Implementation {
  explicit Implementation(PixelCenterControllerSettings value) : settings(value) {}

  PixelCenterControllerSettings settings;
  PixelCenterControllerStatus status{
      .state = PixelCenterControllerState::holding,
      .last_rejection_reason = std::nullopt,
  };
  std::optional<SelectedTargetObservation> last_tracking;
  std::optional<std::tuple<std::string, std::string, std::uint64_t, std::uint64_t, TrackingState>>
      last_observation_key;
  std::optional<std::tuple<std::string, std::string, std::uint64_t>> last_image_key;
  bool stale_emitted{true};
};

PixelCenterController::PixelCenterController(PixelCenterControllerSettings settings)
    : implementation_(std::make_unique<Implementation>(settings)) {}

PixelCenterController::~PixelCenterController() = default;

std::optional<PanTiltDelta> PixelCenterController::process(
    const SelectedTargetObservation& observation, std::int64_t now_unix_ns) {
  auto& state = *implementation_;
  validate(observation);
  const auto image_key = std::tuple{
      observation.source_instance_id, observation.tracker_instance_id, observation.sequence};
  const auto observation_key = std::tuple{
      observation.source_instance_id, observation.tracker_instance_id, observation.sequence,
      observation.selected_track_id, observation.tracking_state};
  if (state.last_image_key && std::get<0>(image_key) == std::get<0>(*state.last_image_key) &&
      std::get<1>(image_key) == std::get<1>(*state.last_image_key)) {
    if (std::get<2>(image_key) < std::get<2>(*state.last_image_key)) {
      ++state.status.out_of_order_observations;
      state.status.last_rejection_reason = "OUT_OF_ORDER";
      return zero_command(observation, now_unix_ns, ControllerDecision::out_of_order);
    }
    if (state.last_observation_key && observation_key == *state.last_observation_key) {
      ++state.status.duplicate_observations;
      state.status.last_rejection_reason = "DUPLICATE";
      return zero_command(observation, now_unix_ns, ControllerDecision::duplicate);
    }
  }
  state.last_observation_key = observation_key;

  if (observation.tracking_state == TrackingState::no_target) {
    state.last_tracking.reset();
    state.stale_emitted = true;
    state.status.state = PixelCenterControllerState::holding;
    state.status.last_rejection_reason = "NO_TARGET";
    return zero_command(observation, now_unix_ns, ControllerDecision::no_target);
  }
  if (observation.tracking_state == TrackingState::lost) {
    state.stale_emitted = true;
    state.status.state = PixelCenterControllerState::holding;
    state.status.last_rejection_reason = "LOST";
    return zero_command(observation, now_unix_ns, ControllerDecision::lost);
  }
  if (observation.tracking_state == TrackingState::missing) {
    state.last_tracking = observation;
    state.last_image_key = image_key;
    state.stale_emitted = false;
    state.status.state = PixelCenterControllerState::holding;
    state.status.last_delta_pan_deg = 0;
    state.status.last_delta_tilt_deg = 0;
    state.status.observation_age_ms =
        (now_unix_ns - observation.captured_at_unix_ns) / 1'000'000.0;
    state.status.last_rejection_reason = "MISSING_HOLD";
    return zero_command(observation, now_unix_ns, ControllerDecision::missing_hold);
  }

  state.last_tracking = observation;
  state.stale_emitted = false;
  const double age_ms = (now_unix_ns - observation.captured_at_unix_ns) / 1'000'000.0;
  state.status.observation_age_ms = age_ms;
  if (age_ms < -50 || age_ms > state.settings.observation_timeout_ms) {
    state.stale_emitted = true;
    ++state.status.stale_observations;
    state.status.state = PixelCenterControllerState::holding;
    state.status.last_rejection_reason = "STALE";
    return zero_command(observation, now_unix_ns, ControllerDecision::stale);
  }

  if (state.last_image_key && image_key == *state.last_image_key) {
    ++state.status.duplicate_observations;
    state.status.last_rejection_reason = "IMAGE_ALREADY_ACTED";
    return zero_command(observation, now_unix_ns, ControllerDecision::duplicate);
  }
  state.last_image_key = image_key;

  const float error_x = observation.target_center_x - observation.image_width / 2.0F;
  const float error_y = observation.target_center_y - observation.image_height / 2.0F;
  const float pan = std::abs(error_x) <= state.settings.deadband_x_px
                        ? 0.0F
                        : std::clamp(state.settings.kp_pan_deg_per_px * error_x,
                                     -state.settings.max_pan_step_deg,
                                     state.settings.max_pan_step_deg);
  const float tilt = std::abs(error_y) <= state.settings.deadband_y_px
                         ? 0.0F
                         : std::clamp(state.settings.kp_tilt_deg_per_px * error_y,
                                      -state.settings.max_tilt_step_deg,
                                      state.settings.max_tilt_step_deg);
  ++state.status.processed_observations;
  state.status.state = PixelCenterControllerState::active;
  state.status.error_x_px = error_x;
  state.status.error_y_px = error_y;
  state.status.last_delta_pan_deg = pan;
  state.status.last_delta_tilt_deg = tilt;
  state.status.last_rejection_reason.reset();
  return PanTiltDelta{
      .source_instance_id = observation.source_instance_id,
      .tracker_instance_id = observation.tracker_instance_id,
      .sequence = observation.sequence,
      .captured_at_unix_ns = observation.captured_at_unix_ns,
      .computed_at_unix_ns = now_unix_ns,
      .selected_track_id = observation.selected_track_id,
      .delta_pan_deg = pan,
      .delta_tilt_deg = tilt,
      .reason = pan == 0 && tilt == 0 ? ControllerDecision::deadband : ControllerDecision::applied,
  };
}

std::optional<PanTiltDelta> PixelCenterController::check_timeout(std::int64_t now_unix_ns) {
  auto& state = *implementation_;
  if (!state.last_tracking || state.stale_emitted) return std::nullopt;
  const double age_ms = (now_unix_ns - state.last_tracking->captured_at_unix_ns) / 1'000'000.0;
  state.status.observation_age_ms = age_ms;
  if (age_ms <= state.settings.observation_timeout_ms) return std::nullopt;
  state.stale_emitted = true;
  ++state.status.stale_observations;
  state.status.state = PixelCenterControllerState::holding;
  state.status.last_delta_pan_deg = 0;
  state.status.last_delta_tilt_deg = 0;
  state.status.last_rejection_reason = "STALE";
  return zero_command(*state.last_tracking, now_unix_ns, ControllerDecision::stale);
}

PixelCenterControllerStatus PixelCenterController::status() const { return implementation_->status; }

struct PixelCenterControllerService::Implementation {
  Implementation(PixelCenterControllerSettings value, TransportPort& port)
      : settings(value), transport(port), controller(value) {}

  PixelCenterControllerSettings settings;
  TransportPort& transport;
  PixelCenterController controller;
  std::mutex mutex;
  std::optional<SelectedTargetObservation> latest;
};

PixelCenterControllerService::PixelCenterControllerService(
    PixelCenterControllerSettings settings, TransportPort& transport)
    : implementation_(std::make_unique<Implementation>(settings, transport)) {}

PixelCenterControllerService::~PixelCenterControllerService() = default;

void PixelCenterControllerService::run(std::stop_token stop_token) {
  auto& state = *implementation_;
  state.transport.start([&state](SelectedTargetObservation observation) {
    std::lock_guard lock(state.mutex);
    state.latest = std::move(observation);
  });
  const auto period = std::chrono::microseconds(1'000'000 / state.settings.control_rate_hz);
  auto last_status_at = std::chrono::steady_clock::time_point{};
  while (!stop_token.stop_requested()) {
    std::optional<SelectedTargetObservation> observation;
    {
      std::lock_guard lock(state.mutex);
      observation = std::move(state.latest);
      state.latest.reset();
    }
    const auto now_unix_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    if (observation) {
      if (auto command = state.controller.process(*observation, now_unix_ns)) {
        state.transport.publish_delta(*command);
      }
    } else if (auto command = state.controller.check_timeout(now_unix_ns)) {
      state.transport.publish_delta(*command);
    }
    const auto steady_now = std::chrono::steady_clock::now();
    if (last_status_at == std::chrono::steady_clock::time_point{} ||
        steady_now - last_status_at >= std::chrono::seconds(1)) {
      state.transport.publish_status(state.controller.status());
      last_status_at = steady_now;
    }
    std::this_thread::sleep_for(period);
  }
  state.transport.stop();
  auto stopped = state.controller.status();
  stopped.state = PixelCenterControllerState::stopped;
  state.transport.publish_status(stopped);
}

}  // namespace face_tracking::controller
