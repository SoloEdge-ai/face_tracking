#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <string>

#include "face_tracking/schemas/codec.hpp"
#include "face_tracking/schemas/settings.hpp"

namespace {
std::vector<std::uint8_t> fixture(const std::string& name) {
  std::ifstream input(std::string(FACE_TRACKING_FIXTURE_DIR) + "/" + name);
  std::string hex;
  input >> hex;
  std::vector<std::uint8_t> bytes;
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(index, 2), nullptr, 16)));
  }
  return bytes;
}
}

TEST(Schemas, FrameMetadataRoundTrips) {
  face_tracking::FrameMetadata value{.source_instance_id = "camera-a", .sequence = 7, .captured_at_unix_ns = 123456, .width = 1280, .height = 720, .capture_format = "MJPG", .jpeg_quality = 80};
  const auto encoded = face_tracking::codec::encode(value);
  const auto decoded = face_tracking::codec::decode_frame_metadata(encoded);
  EXPECT_EQ(decoded.schema_version, 2U);
  EXPECT_EQ(decoded.source_instance_id, "camera-a");
  EXPECT_EQ(decoded.sequence, 7U);
  EXPECT_EQ(decoded.width, 1280U);
}

TEST(Schemas, InvalidMetadataIsRejected) {
  face_tracking::FrameMetadata value;
  EXPECT_THROW(face_tracking::validate(value), std::invalid_argument);
}

TEST(Schemas, PythonGoldenFrameMetadataDecodes) {
  const auto bytes = fixture("frame_metadata_v2.hex");
  const auto decoded = face_tracking::codec::decode_frame_metadata(bytes);
  EXPECT_EQ(face_tracking::codec::encode(decoded), bytes);
  EXPECT_EQ(decoded.schema_version, 2U);
  EXPECT_EQ(decoded.source_instance_id, "camera-a");
  EXPECT_EQ(decoded.sequence, 7U);
  EXPECT_EQ(decoded.captured_at_unix_ns, 123456);
  EXPECT_EQ(decoded.width, 1280U);
  EXPECT_EQ(decoded.height, 720U);
  EXPECT_EQ(decoded.capture_format, "MJPG");
  EXPECT_EQ(decoded.jpeg_quality, 80U);
}

TEST(Schemas, PythonGoldenDetectionDecodes) {
  const auto bytes = fixture("detection_result_v2.hex");
  const auto decoded = face_tracking::codec::decode_detection_result(bytes);
  EXPECT_EQ(face_tracking::codec::encode(decoded), bytes);
  ASSERT_EQ(decoded.boxes.size(), 1U);
  EXPECT_EQ(decoded.schema_version, 2U);
  EXPECT_EQ(decoded.source_instance_id, "camera-a");
  EXPECT_EQ(decoded.sequence, 7U);
  EXPECT_EQ(decoded.captured_at_unix_ns, 123456);
  EXPECT_EQ(decoded.image_width, 1280U);
  EXPECT_EQ(decoded.image_height, 720U);
  EXPECT_DOUBLE_EQ(decoded.inference_ms, 12.5);
  EXPECT_FLOAT_EQ(decoded.boxes.front().x, 1.0F);
  EXPECT_FLOAT_EQ(decoded.boxes.front().y, 2.0F);
  EXPECT_FLOAT_EQ(decoded.boxes.front().width, 3.0F);
  EXPECT_FLOAT_EQ(decoded.boxes.front().height, 4.0F);
  EXPECT_FLOAT_EQ(decoded.boxes.front().confidence, 0.75F);
}

TEST(Schemas, TrackedDetectionRoundTripsWithProcessIdentity) {
  face_tracking::DetectionResult value{
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = 8,
      .captured_at_unix_ns = 123457,
      .image_width = 1280,
      .image_height = 720,
      .inference_ms = 11.0,
      .boxes = {{.x = 10, .y = 20, .width = 30, .height = 40, .confidence = 0.9F, .track_id = 42}},
  };

  const auto decoded = face_tracking::codec::decode_detection_result(face_tracking::codec::encode(value));

  EXPECT_EQ(decoded.tracker_instance_id, "tracker-a");
  ASSERT_EQ(decoded.boxes.size(), 1U);
  EXPECT_EQ(decoded.boxes.front().track_id, 42U);
}

TEST(Schemas, TargetObservationAndControllerOutputRoundTrip) {
  face_tracking::SelectedTargetObservation observation{
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = 9,
      .captured_at_unix_ns = 2000000000,
      .selected_track_id = 42,
      .target_center_x = 740,
      .target_center_y = 300,
      .image_width = 1280,
      .image_height = 720,
      .tracking_state = face_tracking::TrackingState::tracking,
  };
  const auto decoded_observation = face_tracking::codec::decode_selected_target(
      face_tracking::codec::encode(observation));
  EXPECT_EQ(decoded_observation.selected_track_id, 42U);
  EXPECT_EQ(decoded_observation.tracking_state, face_tracking::TrackingState::tracking);

  face_tracking::PanTiltDelta command{
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = 9,
      .captured_at_unix_ns = 2000000000,
      .computed_at_unix_ns = 2100000000,
      .selected_track_id = 42,
      .delta_pan_deg = 1.0F,
      .delta_tilt_deg = -0.6F,
      .reason = face_tracking::ControllerDecision::applied,
  };
  const auto decoded_command = face_tracking::codec::decode_pan_tilt_delta(
      face_tracking::codec::encode(command));
  EXPECT_FLOAT_EQ(decoded_command.delta_pan_deg, 1.0F);
  EXPECT_EQ(decoded_command.reason, face_tracking::ControllerDecision::applied);

  command.delta_pan_deg = 0;
  command.delta_tilt_deg = 0;
  command.reason = face_tracking::ControllerDecision::duplicate;
  EXPECT_EQ(
      face_tracking::codec::decode_pan_tilt_delta(face_tracking::codec::encode(command)).reason,
      face_tracking::ControllerDecision::duplicate);
  command.reason = face_tracking::ControllerDecision::out_of_order;
  EXPECT_EQ(
      face_tracking::codec::decode_pan_tilt_delta(face_tracking::codec::encode(command)).reason,
      face_tracking::ControllerDecision::out_of_order);
}

TEST(Schemas, MissingTargetAndServoCommandedStateRoundTrip) {
  face_tracking::SelectedTargetObservation missing{
      .source_instance_id = "camera-a",
      .tracker_instance_id = "tracker-a",
      .sequence = 10,
      .captured_at_unix_ns = 2'200'000'000,
      .selected_track_id = 42,
      .image_width = 1280,
      .image_height = 720,
      .tracking_state = face_tracking::TrackingState::missing,
  };
  EXPECT_EQ(
      face_tracking::codec::decode_selected_target(face_tracking::codec::encode(missing))
          .tracking_state,
      face_tracking::TrackingState::missing);

  face_tracking::PanTiltCommandedState state{
      .updated_at_unix_ns = 2'300'000'000,
      .commanded_pan_deg = 135.0F,
      .commanded_tilt_deg = 20.0F,
      .last_track_id = 42,
      .state = face_tracking::ServoDriverState::holding,
      .decision = face_tracking::ServoDecision::held_missing,
      .pan_limit_held = false,
      .tilt_limit_held = true,
      .pwm_active = true,
      .applied_commands = 7,
      .rejected_commands = 1,
  };
  const auto decoded = face_tracking::codec::decode_pan_tilt_commanded_state(
      face_tracking::codec::encode(state));
  EXPECT_FLOAT_EQ(decoded.commanded_pan_deg, 135.0F);
  EXPECT_FLOAT_EQ(decoded.commanded_tilt_deg, 20.0F);
  EXPECT_EQ(decoded.decision, face_tracking::ServoDecision::held_missing);
  EXPECT_TRUE(decoded.tilt_limit_held);
  EXPECT_TRUE(decoded.pwm_active);
}

TEST(Schemas, PythonGoldenServoCommandedStateRoundTripsByteForByte) {
  const auto bytes = fixture("pan_tilt_commanded_state_v2.hex");
  const auto decoded = face_tracking::codec::decode_pan_tilt_commanded_state(bytes);
  EXPECT_EQ(face_tracking::codec::encode(decoded), bytes);
  EXPECT_FLOAT_EQ(decoded.commanded_pan_deg, 135.0F);
  EXPECT_FLOAT_EQ(decoded.commanded_tilt_deg, 20.0F);
  EXPECT_EQ(decoded.decision, face_tracking::ServoDecision::held_missing);
  EXPECT_TRUE(decoded.pwm_active);
}

TEST(Schemas, PythonGoldenCameraStatusRoundTripsByteForByte) {
  const auto bytes = fixture("camera_status_v2.hex");
  const auto decoded = face_tracking::codec::decode_camera_status(bytes);
  EXPECT_EQ(face_tracking::codec::encode(decoded), bytes);
  EXPECT_EQ(decoded.state, face_tracking::CameraState::streaming);
  EXPECT_DOUBLE_EQ(decoded.capture_fps, 29.5);
  EXPECT_DOUBLE_EQ(decoded.publish_fps, 9.75);
  EXPECT_EQ(decoded.captured_frames, 100U);
  EXPECT_EQ(decoded.published_frames, 33U);
  EXPECT_EQ(decoded.dropped_frames, 67U);
  EXPECT_EQ(decoded.device_path, "/dev/video0");
  EXPECT_FALSE(decoded.last_error);
}

TEST(Schemas, PythonGoldenDetectorStatusRoundTripsByteForByte) {
  const auto bytes = fixture("detector_status_v2.hex");
  const auto decoded = face_tracking::codec::decode_detector_status(bytes);
  EXPECT_EQ(face_tracking::codec::encode(decoded), bytes);
  EXPECT_EQ(decoded.state, face_tracking::DetectorState::error);
  EXPECT_DOUBLE_EQ(decoded.inference_fps, 4.25);
  EXPECT_EQ(decoded.processed_frames, 20U);
  EXPECT_EQ(decoded.dropped_frames, 10U);
  EXPECT_EQ(decoded.decode_errors, 2U);
  EXPECT_EQ(decoded.inference_errors, 1U);
  ASSERT_TRUE(decoded.last_error);
  EXPECT_EQ(*decoded.last_error, "boom");
}

TEST(Settings, CameraProcessLoadsOnlyItsOwnTypedSection) {
  const auto path = std::filesystem::temp_directory_path() / "face_tracking_camera_settings_test.yaml";
  {
    std::ofstream output(path);
    output << "common: {device_id: pi}\n"
              "middleware: {adapter: future, connect: tcp/127.0.0.1:7447, key_prefix: face_tracking}\n"
              "camera: {device_path: /dev/video0, width: 1280, height: 720, capture_fps: 30, publish_hz: 10, jpeg_quality: 80, reconnect_seconds: 2.0}\n";
  }
  const auto settings = face_tracking::load_camera_process_settings(path);
  std::filesystem::remove(path);
  EXPECT_EQ(settings.transport.middleware.adapter, "future");
  EXPECT_EQ(settings.camera.width, 1280);
}

TEST(Settings, DetectorProcessLoadsOnlyItsOwnTypedSection) {
  const auto path = std::filesystem::temp_directory_path() / "face_tracking_detector_settings_test.yaml";
  {
    std::ofstream output(path);
    output << "common: {device_id: pi}\n"
              "middleware: {adapter: zenoh, connect: tcp/127.0.0.1:7447, key_prefix: face_tracking}\n"
              "detector: {model_path: model.onnx, inference_hz: 5, image_size: 640, confidence: 0.5, iou: 0.45, tracker: {retention_ms: 1000, min_match_iou: 0.1, max_center_distance_ratio: 1.0}}\n";
  }
  const auto settings = face_tracking::load_detector_process_settings(path);
  std::filesystem::remove(path);
  EXPECT_EQ(settings.transport.camera_image_key(), "face_tracking/pi/camera/image");
  EXPECT_EQ(settings.detector.image_size, 640);
}

TEST(Settings, ControllerLoadsTypedSettingsAndKeys) {
  const auto path = std::filesystem::temp_directory_path() / "face_tracking_controller_settings_test.yaml";
  {
    std::ofstream output(path);
    output << "common: {device_id: pi}\n"
              "middleware: {adapter: zenoh, connect: tcp/127.0.0.1:7447, key_prefix: face_tracking}\n"
              "controller: {control_rate_hz: 20, deadband_x_px: 30, deadband_y_px: 24, kp_pan_deg_per_px: 0.01, kp_tilt_deg_per_px: 0.01, max_pan_step_deg: 1.5, max_tilt_step_deg: 1.0, observation_timeout_ms: 200}\n";
  }
  const auto settings = face_tracking::load_controller_process_settings(path);
  std::filesystem::remove(path);
  EXPECT_EQ(settings.controller.control_rate_hz, 20);
  EXPECT_EQ(settings.transport.selected_target_key(), "face_tracking/pi/target/selected");
  EXPECT_EQ(settings.transport.pan_tilt_delta_key(), "face_tracking/pi/pan_tilt/delta_cmd");
}

TEST(Settings, ServoLoadsIndependentAxisLimitsAndKeys) {
  const auto path = std::filesystem::temp_directory_path() / "face_tracking_servo_settings_test.yaml";
  {
    std::ofstream output(path);
    output << "common: {device_id: pi}\n"
              "middleware: {adapter: zenoh, connect: tcp/127.0.0.1:7447, key_prefix: face_tracking}\n"
              "servo: {gpio_chip: 0, frequency_hz: 50, upstream_timeout_ms: 1500, tracking_enabled: true, "
              "pan: {gpio: 17, rated_max_deg: 270, min_deg: 0, max_deg: 270, home_deg: 135, min_pulse_us: 500, max_pulse_us: 2500, invert: false}, "
              "tilt: {gpio: 27, rated_max_deg: 180, min_deg: 15, max_deg: 45, home_deg: 20, min_pulse_us: 500, max_pulse_us: 2500, invert: false}}\n";
  }
  const auto settings = face_tracking::load_servo_process_settings(path);
  std::filesystem::remove(path);
  EXPECT_EQ(settings.servo.pan.gpio, 17);
  EXPECT_FLOAT_EQ(settings.servo.tilt.home_deg, 20.0F);
  EXPECT_FLOAT_EQ(settings.servo.tilt.min_deg, 15.0F);
  EXPECT_EQ(settings.transport.pan_tilt_commanded_state_key(),
            "face_tracking/pi/pan_tilt/commanded_state");
  EXPECT_EQ(settings.transport.servo_liveliness_key(), "face_tracking/pi/liveliness/servo_driver");
}
