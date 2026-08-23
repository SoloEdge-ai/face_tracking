#include <gtest/gtest.h>

#include <tuple>
#include <vector>

#include "face_tracking/servo/servo_driver.hpp"

namespace {
class FakePwm final : public face_tracking::servo::ServoPwmPort {
 public:
  void start(int chip) override { gpio_chip = chip; }
  void set_pulse(int gpio, int pulse_us, int frequency_hz) override {
    pulses.emplace_back(gpio, pulse_us, frequency_hz);
  }
  void stop() noexcept override { stopped = true; }

  int gpio_chip{-1};
  bool stopped{};
  std::vector<std::tuple<int, int, int>> pulses;
};

face_tracking::ServoDriverSettings settings() {
  return {
      .gpio_chip = 0,
      .frequency_hz = 50,
      .upstream_timeout_ms = 1500,
      .tracking_enabled = true,
      .pan = {.gpio = 17, .rated_max_deg = 270, .min_deg = 0, .max_deg = 270,
              .home_deg = 135, .min_pulse_us = 500, .max_pulse_us = 2500},
      .tilt = {.gpio = 27, .rated_max_deg = 180, .min_deg = 15, .max_deg = 45,
               .home_deg = 10, .min_pulse_us = 500, .max_pulse_us = 2500},
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
  EXPECT_EQ(pwm.pulses[0], std::make_tuple(17, 1500, 50));
  EXPECT_EQ(pwm.pulses[1], std::make_tuple(27, 611, 50));
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 10);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::home_startup);
  EXPECT_TRUE(state.pwm_active);
}

TEST(ServoDriver, HoldsOnlyTheAxisWhoseCandidateWouldExceedItsLimit) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(0, 29, face_tracking::ControllerDecision::applied), 1'100'000'001);
  const auto& state = driver.process(
      command(1, 2, face_tracking::ControllerDecision::applied, 2), 1'100'000'002);
  EXPECT_FLOAT_EQ(state.commanded_pan_deg, 136);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 44);
  EXPECT_FALSE(state.pan_limit_held);
  EXPECT_TRUE(state.tilt_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, EntersTiltTrackingRangeFromHomeBeforeApplyingDelta) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  const auto& state = driver.process(
      command(0, -1, face_tracking::ControllerDecision::applied), 1'100'000'001);
  EXPECT_FLOAT_EQ(state.commanded_tilt_deg, 15);
  EXPECT_TRUE(state.tilt_limit_held);
  EXPECT_EQ(state.decision, face_tracking::ServoDecision::held_limit);
}

TEST(ServoDriver, MissingHoldsAndLostReturnsHome) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(5, 5, face_tracking::ControllerDecision::applied), 1'100'000'001);
  const auto& held = driver.process(
      command(0, 0, face_tracking::ControllerDecision::missing_hold, 2), 1'100'000'002);
  EXPECT_FLOAT_EQ(held.commanded_pan_deg, 140);
  EXPECT_FLOAT_EQ(held.commanded_tilt_deg, 20);
  EXPECT_EQ(held.decision, face_tracking::ServoDecision::held_missing);
  const auto& home = driver.process(
      command(0, 0, face_tracking::ControllerDecision::lost, 3), 1'100'000'003);
  EXPECT_FLOAT_EQ(home.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(home.commanded_tilt_deg, 10);
  EXPECT_EQ(home.decision, face_tracking::ServoDecision::home_lost);
}

TEST(ServoDriver, StaleForTheLastSeenSequenceStillReturnsHome) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'100'000'000);
  driver.process(command(5, 5, face_tracking::ControllerDecision::applied), 1'100'000'001);
  driver.process(
      command(0, 0, face_tracking::ControllerDecision::missing_hold, 2), 1'100'000'002);
  const auto& home = driver.process(
      command(0, 0, face_tracking::ControllerDecision::stale, 2), 1'100'000'003);
  EXPECT_FLOAT_EQ(home.commanded_pan_deg, 135);
  EXPECT_FLOAT_EQ(home.commanded_tilt_deg, 10);
  EXPECT_EQ(home.decision, face_tracking::ServoDecision::home_stale);
}

TEST(ServoDriver, ReturnsHomeAfterUpstreamTimeout) {
  FakePwm pwm;
  face_tracking::servo::ServoDriver driver(settings(), pwm);
  driver.start(1'000'000'000);
  driver.process(command(5, 5, face_tracking::ControllerDecision::applied), 1'100'000'000);
  EXPECT_FALSE(driver.check_timeout(2'599'999'999));
  EXPECT_TRUE(driver.check_timeout(2'600'000'000));
  EXPECT_FLOAT_EQ(driver.state().commanded_pan_deg, 135);
  EXPECT_EQ(driver.state().decision, face_tracking::ServoDecision::home_upstream_timeout);
}
