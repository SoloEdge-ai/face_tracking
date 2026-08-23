#include <gtest/gtest.h>

#include "face_tracking/controller/pixel_center_controller.hpp"

namespace {
face_tracking::PixelCenterControllerSettings settings() {
  return {
      .control_rate_hz = 20,
      .deadband_x_px = 30,
      .deadband_y_px = 24,
      .kp_pan_deg_per_px = 0.01F,
      .kp_tilt_deg_per_px = 0.01F,
      .max_pan_step_deg = 1.5F,
      .max_tilt_step_deg = 1.0F,
      .observation_timeout_ms = 200,
  };
}

face_tracking::SelectedTargetObservation observation(
    float center_x, float center_y, std::uint64_t sequence = 1, std::uint64_t track_id = 7) {
  return {
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = sequence,
      .captured_at_unix_ns = 1'000'000'000,
      .selected_track_id = track_id,
      .target_center_x = center_x,
      .target_center_y = center_y,
      .image_width = 1280,
      .image_height = 720,
      .tracking_state = face_tracking::TrackingState::tracking,
  };
}
}  // namespace

TEST(PixelCenterController, AppliesDirectionDeadbandAndStepLimits) {
  face_tracking::controller::PixelCenterController controller(settings());
  const auto command = controller.process(observation(740, 300), 1'100'000'000);
  ASSERT_TRUE(command);
  EXPECT_FLOAT_EQ(command->delta_pan_deg, 1.0F);
  EXPECT_FLOAT_EQ(command->delta_tilt_deg, -0.6F);
  EXPECT_EQ(command->reason, face_tracking::ControllerDecision::applied);

  const auto deadband = controller.process(observation(670, 384, 2), 1'100'000'000);
  ASSERT_TRUE(deadband);
  EXPECT_FLOAT_EQ(deadband->delta_pan_deg, 0.0F);
  EXPECT_FLOAT_EQ(deadband->delta_tilt_deg, 0.0F);
  EXPECT_EQ(deadband->reason, face_tracking::ControllerDecision::deadband);

  const auto clamped = controller.process(observation(1280, 0, 3), 1'100'000'000);
  ASSERT_TRUE(clamped);
  EXPECT_FLOAT_EQ(clamped->delta_pan_deg, 1.5F);
  EXPECT_FLOAT_EQ(clamped->delta_tilt_deg, -1.0F);
}

TEST(PixelCenterController, RejectsDuplicateOutOfOrderAndStaleObservations) {
  face_tracking::controller::PixelCenterController controller(settings());
  ASSERT_TRUE(controller.process(observation(700, 360, 2), 1'100'000'000));
  const auto duplicate = controller.process(observation(700, 360, 2), 1'100'000'001);
  ASSERT_TRUE(duplicate);
  EXPECT_EQ(duplicate->reason, face_tracking::ControllerDecision::duplicate);
  EXPECT_FLOAT_EQ(duplicate->delta_pan_deg, 0.0F);
  EXPECT_FLOAT_EQ(duplicate->delta_tilt_deg, 0.0F);
  const auto out_of_order = controller.process(observation(700, 360, 1), 1'100'000'002);
  ASSERT_TRUE(out_of_order);
  EXPECT_EQ(out_of_order->reason, face_tracking::ControllerDecision::out_of_order);
  EXPECT_FLOAT_EQ(out_of_order->delta_pan_deg, 0.0F);
  EXPECT_FLOAT_EQ(out_of_order->delta_tilt_deg, 0.0F);

  const auto stale = controller.check_timeout(1'250'000'000);
  ASSERT_TRUE(stale);
  EXPECT_EQ(stale->reason, face_tracking::ControllerDecision::stale);
  EXPECT_FLOAT_EQ(stale->delta_pan_deg, 0.0F);
  EXPECT_FALSE(controller.check_timeout(1'300'000'000));

  const auto status = controller.status();
  EXPECT_EQ(status.duplicate_observations, 1U);
  EXPECT_EQ(status.out_of_order_observations, 1U);
  EXPECT_EQ(status.stale_observations, 1U);
}

TEST(PixelCenterController, LostTargetProducesOneZeroHoldCommand) {
  face_tracking::controller::PixelCenterController controller(settings());
  auto lost = observation(700, 360);
  lost.tracking_state = face_tracking::TrackingState::lost;
  const auto command = controller.process(lost, 1'100'000'000);
  ASSERT_TRUE(command);
  EXPECT_EQ(command->reason, face_tracking::ControllerDecision::lost);
  EXPECT_FLOAT_EQ(command->delta_pan_deg, 0.0F);
  EXPECT_FLOAT_EQ(command->delta_tilt_deg, 0.0F);
}

TEST(PixelCenterController, AllowsSwitchingTargetsWithinTheSameDetectionFrame) {
  face_tracking::controller::PixelCenterController controller(settings());
  const auto first = controller.process(observation(740, 360, 5, 7), 1'100'000'000);
  ASSERT_TRUE(first);
  EXPECT_GT(first->delta_pan_deg, 0.0F);

  const auto switched = controller.process(observation(540, 360, 5, 8), 1'100'000'001);
  ASSERT_TRUE(switched);
  EXPECT_FLOAT_EQ(switched->delta_pan_deg, 0.0F);
  EXPECT_EQ(switched->reason, face_tracking::ControllerDecision::duplicate);
  EXPECT_EQ(switched->selected_track_id, 8U);
}
