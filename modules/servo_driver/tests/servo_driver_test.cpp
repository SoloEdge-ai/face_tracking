#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <tuple>
#include <vector>

#include "face_tracking/servo/servo_driver.hpp"
#include "face_tracking/servo/hardware_pwm.hpp"
#include "face_tracking/servo/servo_sweep.hpp"

namespace {
class FakePwm final : public face_tracking::servo::ServoPwmPort {
 public:
  void start(int chip) override { pwm_chip = chip; }
  void set_pulse(int gpio, int pulse_us, int frequency_hz) override {
    pulses.emplace_back(gpio, pulse_us, frequency_hz);
  }
  void stop() noexcept override { stopped = true; }

  int pwm_chip{-1};
  bool stopped{};
  std::vector<std::tuple<int, int, int>> pulses;
};

class FakeTransport final : public face_tracking::servo::TransportPort {
 public:
  void start(CommandHandler command, UpstreamHandler upstream) override {
    std::lock_guard lock(mutex);
    command_handler = std::move(command);
    upstream_handler = std::move(upstream);
    started = true;
    condition.notify_all();
  }
  void stop() override {}
  void publish_state(const face_tracking::PanTiltCommandedState& state) override {
    std::lock_guard lock(mutex);
    states.push_back(state);
    condition.notify_all();
  }
  void wait_until_started() {
    std::unique_lock lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [this] { return started; }));
  }
  void emit(face_tracking::servo::UpstreamEvent event) { upstream_handler(event); }
  void emit(face_tracking::PanTiltDelta command) { command_handler(std::move(command)); }
  bool wait_for_state(face_tracking::ServoDecision decision, float pan, float tilt) {
    std::unique_lock lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(1), [&] {
      return !states.empty() && states.back().decision == decision &&
             states.back().commanded_pan_deg == pan && states.back().commanded_tilt_deg == tilt;
    });
  }

 private:
  std::mutex mutex;
  std::condition_variable condition;
  CommandHandler command_handler;
  UpstreamHandler upstream_handler;
  bool started{};
  std::vector<face_tracking::PanTiltCommandedState> states;
};

std::int64_t system_now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

face_tracking::ServoDriverSettings settings() {
  return {
      .pwm_chip = 0,
      .frequency_hz = 50,
      .command_max_age_ms = 1500,
      .upstream_timeout_ms = 5000,
      .max_input_pan_delta_deg = 1.5,
      .max_input_tilt_delta_deg = 1.0,
      .tracking_enabled = true,
      .pan = {.gpio = 18, .rated_max_deg = 270, .min_deg = 100, .max_deg = 170,
              .home_deg = 135, .min_pulse_us = 500, .max_pulse_us = 2500},
      .tilt = {.gpio = 19, .rated_max_deg = 180, .min_deg = 15, .max_deg = 45,
               .home_deg = 20, .min_pulse_us = 500, .max_pulse_us = 2500},
  };
}

face_tracking::PanTiltDelta command(
    float pan, float tilt, face_tracking::ControllerDecision reason, std::uint64_t sequence = 1) {
  return {
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = sequence,
      .captured_at_unix_ns = 1'000'000'000,
      .computed_at_unix_ns = 1'100'000'000,
      .selected_track_id = 7,
      .delta_pan_deg = pan,
      .delta_tilt_deg = tilt,
      .reason = reason,
  };
}
}

TEST(ServoDriver, StartsAtHomeWithNominalPulseMapping) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  const auto& state = driver.start(1'100'000'000);
  ASSERT_EQ(pwm.pulses.size(), 2U);
  EXPECT_EQ(pwm.pulses[0], std::make_tuple(18, 1500, 50));
  EXPECT_EQ(pwm.pulses[1], std::make_tuple(19, 722, 50));
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 20);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::home_startup);
  EXPECT_TRUE(state.pwm_active);
}

TEST(HardwarePwm, MapsRaspberryPi5HeaderPinsToIndependentChannels) {
  using face_tracking::servo::LinuxHardwareServoPwm;
  EXPECT_EQ(LinuxHardwareServoPwm::channel_for_gpio(18), 2);
  EXPECT_EQ(LinuxHardwareServoPwm::channel_for_gpio(19), 3);
  EXPECT_THROW((void)LinuxHardwareServoPwm::channel_for_gpio(17), std::invalid_argument);
  EXPECT_THROW((void)LinuxHardwareServoPwm::channel_for_gpio(27), std::invalid_argument);
}

TEST(ServoDriver, HoldsOnlyTheAxisWhoseCandidateWouldExceedItsLimit) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  for (std::uint64_t sequence = 1; sequence <= 25; ++sequence) {
    driver.process(command(0, 1, face_tracking::ControllerDecision::applied, sequence),
                   1'100'000'000 + static_cast<std::int64_t>(sequence));
  }
  const auto& state = driver.process(
      command(1, 1, face_tracking::ControllerDecision::applied, 26), 1'100'000'026);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 45);
  EXPECT_FALSE(state.pan_limit_held);
  EXPECT_TRUE(state.tilt_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, HoldsTiltAtMinimumBoundary) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  for (std::uint64_t sequence = 1; sequence <= 5; ++sequence) {
    driver.process(command(0, -1, face_tracking::ControllerDecision::applied, sequence),
                   1'100'000'000 + static_cast<std::int64_t>(sequence));
  }
  const auto& state = driver.process(
      command(0, -1, face_tracking::ControllerDecision::applied, 6), 1'100'000'006);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 15);
  EXPECT_TRUE(state.tilt_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, HoldsPanAtMaximumBoundary) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  for (std::uint64_t sequence = 1; sequence <= 35; ++sequence) {
    driver.process(command(1, 0, face_tracking::ControllerDecision::applied, sequence),
                   1'100'000'000 + static_cast<std::int64_t>(sequence));
  }
  const auto& state = driver.process(
      command(1, 0, face_tracking::ControllerDecision::applied, 36), 1'100'000'036);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 170);
  EXPECT_TRUE(state.pan_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, HoldsPanAtMinimumBoundary) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  for (std::uint64_t sequence = 1; sequence <= 35; ++sequence) {
    driver.process(command(-1, 0, face_tracking::ControllerDecision::applied, sequence),
                   1'100'000'000 + static_cast<std::int64_t>(sequence));
  }
  const auto& state = driver.process(
      command(-1, 0, face_tracking::ControllerDecision::applied, 36), 1'100'000'036);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 100);
  EXPECT_TRUE(state.pan_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, MissingHoldsAndLostReturnsHome) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(1, 1, face_tracking::ControllerDecision::applied), 1'100'000'001);
  const auto& held = driver.process(
      command(0, 0, face_tracking::ControllerDecision::missing_hold, 2), 1'100'000'002);
  EXPECT_FLOAT_EQ(held.commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(held.commanded_tilt_deg, 21);
  EXPECT_EQ(held.decision, face_tracking::ServoDecision::held_missing);
  const auto& home = driver.process(
      command(0, 0, face_tracking::ControllerDecision::lost, 3), 1'100'000'003);
  EXPECT_FLOAT_EQ(home.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(home.commanded_tilt_deg, 20);
  EXPECT_EQ(home.decision, face_tracking::ServoDecision::home_lost);
}

TEST(ServoDriver, StaleHoldsWithoutRefreshingTheFreshControlTimeout) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(1, 1, face_tracking::ControllerDecision::applied), 1'100'000'001);
  driver.process(
      command(0, 0, face_tracking::ControllerDecision::missing_hold, 2), 1'100'000'002);
  const auto& held = driver.process(
      command(0, 0, face_tracking::ControllerDecision::stale, 2), 2'000'000'000);
  EXPECT_FLOAT_EQ(held.commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(held.commanded_tilt_deg, 21);
  EXPECT_EQ(held.decision, face_tracking::ServoDecision::held_stale);
  EXPECT_FALSE(driver.check_timeout(6'100'000'001));
  EXPECT_TRUE(driver.check_timeout(6'100'000'002));
  EXPECT_FLOAT_EQ(driver.state().commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(driver.state().commanded_tilt_deg, 20);
  EXPECT_EQ(driver.state().decision, face_tracking::ServoDecision::home_upstream_timeout);
}

TEST(ServoDriver, AgedAppliedCommandHoldsAndCannotMoveTheServos) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'000'000'000);
  driver.process(command(1, 1, face_tracking::ControllerDecision::applied), 1'100'000'000);

  auto aged = command(1, 1, face_tracking::ControllerDecision::applied, 2);
  aged.computed_at_unix_ns = 2'000'000'000;
  const auto& held = driver.process(aged, 4'000'000'001);

  EXPECT_FLOAT_EQ(held.commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(held.commanded_tilt_deg, 21);
  EXPECT_EQ(held.decision, face_tracking::ServoDecision::held_stale);
}

TEST(ServoDriver, ReturnsHomeAfterUpstreamTimeout) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'000'000'000);
  driver.process(command(1, 1, face_tracking::ControllerDecision::applied), 1'100'000'000);
  EXPECT_FALSE(driver.check_timeout(6'099'999'999));
  EXPECT_TRUE(driver.check_timeout(6'100'000'000));
  EXPECT_FLOAT_EQ(driver.state().commanded_pan_deg, 135);
  EXPECT_EQ(driver.state().decision, face_tracking::ServoDecision::home_upstream_timeout);
}

TEST(ServoDriver, RejectsDeltaBeyondDriverInputLimit) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  const auto pulse_count = pwm.pulses.size();
  const auto& state = driver.process(
      command(1.6F, 0, face_tracking::ControllerDecision::applied), 1'100'000'001);
  EXPECT_EQ(pwm.pulses.size(), pulse_count);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 135);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::rejected_invalid);
  EXPECT_EQ(state.rejected_commands, 1U);
}

TEST(ServoDriver, RampsOutputTowardCommandedTargetAtBoundedVelocity) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  const auto pulses_after_start = pwm.pulses.size();

  driver.process(command(1, 1, face_tracking::ControllerDecision::applied),
                 1'100'000'001);
  EXPECT_EQ(pwm.pulses.size(), pulses_after_start);
  EXPECT_FLOAT_EQ(driver.state().commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(driver.state().commanded_tilt_deg, 21);

  driver.advance(1'100'000'101);
  ASSERT_EQ(pwm.pulses.size(), pulses_after_start + 2);
  EXPECT_EQ(pwm.pulses[pulses_after_start], std::make_tuple(18, 1507, 50));
  EXPECT_EQ(pwm.pulses[pulses_after_start + 1], std::make_tuple(19, 733, 50));

  const auto pulses_after_reach = pwm.pulses.size();
  driver.advance(1'100'000'121);
  EXPECT_EQ(pwm.pulses.size(), pulses_after_reach);
}

TEST(ServoDriver, LostWithInvalidDeltaStillReturnsHome) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(1, 1, face_tracking::ControllerDecision::applied), 1'100'000'001);
  const auto& state = driver.process(
      command(100, 100, face_tracking::ControllerDecision::lost, 2), 1'100'000'002);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 20);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::home_lost);
}

TEST(ServoDriver, RejectedDeltasDoNotPostponeUpstreamTimeout) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'000'000'000);
  driver.process(
      command(100, 0, face_tracking::ControllerDecision::applied), 1'500'000'000);
  driver.process(
      command(100, 0, face_tracking::ControllerDecision::applied, 2), 2'400'000'000);
  EXPECT_FALSE(driver.check_timeout(5'999'999'999));
  EXPECT_TRUE(driver.check_timeout(6'000'000'000));
  EXPECT_EQ(driver.state().decision, face_tracking::ServoDecision::home_upstream_timeout);
}

TEST(ServoDriverService, LivelinessLossLatchesHomeAndDropsQueuedCommands) {
  FakePwm pwm;
  FakeTransport transport;
  face_tracking::servo::ServoDriverService service(settings(), pwm, transport);
  std::jthread worker([&service](std::stop_token stop_token) { service.run(stop_token); });
  transport.wait_until_started();

  transport.emit(face_tracking::servo::UpstreamEvent::online);
  auto first = command(1, 1, face_tracking::ControllerDecision::applied);
  first.captured_at_unix_ns = system_now_ns();
  first.computed_at_unix_ns = first.captured_at_unix_ns;
  transport.emit(first);
  ASSERT_TRUE(transport.wait_for_state(face_tracking::ServoDecision::applied, 136, 21));

  transport.emit(face_tracking::servo::UpstreamEvent::offline);
  auto queued = command(1, 1, face_tracking::ControllerDecision::applied, 2);
  queued.captured_at_unix_ns = system_now_ns();
  queued.computed_at_unix_ns = queued.captured_at_unix_ns;
  transport.emit(queued);
  EXPECT_TRUE(transport.wait_for_state(
      face_tracking::ServoDecision::home_upstream_timeout, 135, 20));
  worker.request_stop();
}

TEST(ServoSweep, StartsAtHomeAndReversesAtTiltMaximum) {
  face_tracking::servo::ServoSweep sweep(settings(), 1.0F);
  EXPECT_FLOAT_EQ(sweep.position().pan_deg, 135);
  EXPECT_FLOAT_EQ(sweep.position().tilt_deg, 20);
  for (int step = 0; step < 25; ++step) sweep.next();
  EXPECT_FLOAT_EQ(sweep.position().tilt_deg, 45);
  sweep.next();
  EXPECT_FLOAT_EQ(sweep.position().tilt_deg, 44);
}

TEST(ServoSweep, AlwaysStaysInsideBothConfiguredLimits) {
  face_tracking::servo::ServoSweep sweep(settings(), 1.0F);
  bool saw_pan_min = false;
  bool saw_pan_max = false;
  bool saw_tilt_min = false;
  bool saw_tilt_max = false;
  for (int tick = 0; tick < 1000; ++tick) {
    const auto& position = sweep.next();
    EXPECT_GE(position.pan_deg, 100);
    EXPECT_LE(position.pan_deg, 170);
    EXPECT_GE(position.tilt_deg, 15);
    EXPECT_LE(position.tilt_deg, 45);
    saw_pan_min |= position.pan_deg == 100;
    saw_pan_max |= position.pan_deg == 170;
    saw_tilt_min |= position.tilt_deg == 15;
    saw_tilt_max |= position.tilt_deg == 45;
  }
  EXPECT_TRUE(saw_pan_min);
  EXPECT_TRUE(saw_pan_max);
  EXPECT_TRUE(saw_tilt_min);
  EXPECT_TRUE(saw_tilt_max);
}
