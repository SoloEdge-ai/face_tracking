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
