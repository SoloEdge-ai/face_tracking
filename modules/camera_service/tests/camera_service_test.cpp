#include <gtest/gtest.h>

#include "face_tracking/camera/camera_service.hpp"
#include "camera_internal.hpp"

namespace {
class FakeOutput final : public face_tracking::camera::OutputPort {
 public:
  void publish_frame(const face_tracking::FrameEvent& frame) override { frames.push_back(frame); }
  void publish_status(const face_tracking::CameraStatus& status) override { statuses.push_back(status); }
  std::vector<face_tracking::FrameEvent> frames;
  std::vector<face_tracking::CameraStatus> statuses;
};

class FakeCameraDevice final : public face_tracking::camera::internal::CameraDevice {
 public:
  [[nodiscard]] bool is_open() const override { return opened; }
  bool open(const std::string&) override {
    ++open_attempts;
    opened = open_attempts > failed_open_attempts;
    return opened;
  }
  void configure(const face_tracking::CameraSettings&) override { ++configure_calls; }
  bool read(cv::Mat& frame) override {
    if (fail_read) return false;
    frame = cv::Mat::zeros(2, 2, CV_8UC3);
    return true;
  }
  void release() override { opened = false; }

  bool opened{};
  bool fail_read{};
  int failed_open_attempts{1};
  int open_attempts{};
  int configure_calls{};
};
}

TEST(CameraService, LatestFrameSlotOverwritesOldFrame) {
  face_tracking::camera::internal::LatestFrameSlot slot;
  slot.update(cv::Mat::zeros(2, 2, CV_8UC3), 10);
  slot.update(cv::Mat::ones(2, 2, CV_8UC3), 20);
  ASSERT_TRUE(slot.snapshot());
  EXPECT_EQ(slot.snapshot()->captured_at_unix_ns, 20);
  EXPECT_EQ(slot.captured_frames(), 2U);
}

TEST(CameraService, GateNeverRepublishesSameCapture) {
  FakeOutput output;
  face_tracking::CameraSettings settings{.width = 1280, .height = 720, .publish_hz = 10, .jpeg_quality = 80};
  face_tracking::camera::internal::PublishGate gate(output, "camera-a", settings);
  const auto start = std::chrono::steady_clock::time_point{};
  gate.accept({0xff, 0xd8, 0xff, 0xd9}, 42);
  EXPECT_TRUE(gate.publish_if_due(start));
  EXPECT_FALSE(gate.publish_if_due(start + std::chrono::milliseconds(100)));
  EXPECT_EQ(output.frames.size(), 1U);
}

TEST(CameraService, GateLimitsPublicationFrequency) {
  FakeOutput output;
  face_tracking::CameraSettings settings{.width = 1280, .height = 720, .publish_hz = 10, .jpeg_quality = 80};
  face_tracking::camera::internal::PublishGate gate(output, "camera-a", settings);
  const auto start = std::chrono::steady_clock::time_point{};
  gate.accept({1}, 1);
  EXPECT_TRUE(gate.publish_if_due(start));
  gate.accept({2}, 2);
  EXPECT_FALSE(gate.publish_if_due(start + std::chrono::milliseconds(99)));
  EXPECT_TRUE(gate.publish_if_due(start + std::chrono::milliseconds(100)));
}

TEST(CameraService, GateDoesNotPublishWhenJpegEncodingProducedNoPayload) {
  FakeOutput output;
  face_tracking::CameraSettings settings{.width = 1280, .height = 720, .publish_hz = 10, .jpeg_quality = 80};
  face_tracking::camera::internal::PublishGate gate(output, "camera-a", settings);
  gate.accept({}, 42);
  EXPECT_FALSE(gate.publish_if_due(std::chrono::steady_clock::time_point{}));
  EXPECT_TRUE(output.frames.empty());
}

TEST(CameraService, SuccessfulJpegRecoversOnlyJpegErrors) {
  auto state = face_tracking::CameraState::error;
  std::optional<std::string> error{"JPEG encoding failed"};
  face_tracking::camera::internal::recover_jpeg_error(state, error);
  EXPECT_EQ(state, face_tracking::CameraState::streaming);
  EXPECT_FALSE(error);

  state = face_tracking::CameraState::reconnecting;
  error = "camera capture failed";
  face_tracking::camera::internal::recover_jpeg_error(state, error);
  EXPECT_EQ(state, face_tracking::CameraState::reconnecting);
  EXPECT_EQ(error, "camera capture failed");
}

TEST(CameraService, CaptureBackendReconnectsAfterOpenAndReadFailures) {
  FakeCameraDevice device;
  face_tracking::camera::internal::LatestFrameSlot slot;
  face_tracking::CameraSettings settings{
      .device_path = "/dev/video0", .width = 1280, .height = 720, .capture_fps = 30,
      .publish_hz = 10, .jpeg_quality = 80, .reconnect_seconds = 2.0};
  face_tracking::CameraState state = face_tracking::CameraState::starting;
  std::optional<std::string> error;
  const auto set_state = [&](face_tracking::CameraState value,
                             std::optional<std::string> message) {
    state = value;
    error = std::move(message);
  };

  EXPECT_FALSE(face_tracking::camera::internal::capture_once(device, settings, slot, set_state, 1));
  EXPECT_EQ(state, face_tracking::CameraState::reconnecting);
  EXPECT_TRUE(error.has_value());

  device.fail_read = true;
  EXPECT_FALSE(face_tracking::camera::internal::capture_once(device, settings, slot, set_state, 2));
  EXPECT_EQ(state, face_tracking::CameraState::error);
  EXPECT_FALSE(device.is_open());

  device.fail_read = false;
  EXPECT_TRUE(face_tracking::camera::internal::capture_once(device, settings, slot, set_state, 3));
  EXPECT_EQ(state, face_tracking::CameraState::streaming);
  ASSERT_TRUE(slot.snapshot());
  EXPECT_EQ(slot.snapshot()->captured_at_unix_ns, 3);
  EXPECT_EQ(device.open_attempts, 3);
  EXPECT_EQ(device.configure_calls, 2);
}
