#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <opencv2/imgcodecs.hpp>
#include "face_tracking/detector/detector_service.hpp"
#include "detector_internal.hpp"

namespace {
class FakeTransport final : public face_tracking::detector::TransportPort {
 public:
  void start(FrameHandler value) override { handler = std::move(value); }
  void stop() override { handler = {}; }
  void publish_detection(const face_tracking::DetectionResult& result) override { detections.push_back(result); }
  void publish_status(const face_tracking::DetectorStatus&) override {}
  FrameHandler handler;
  std::vector<face_tracking::DetectionResult> detections;
};

class FakeEngine final : public face_tracking::detector::internal::InferenceEngine {
 public:
  std::vector<face_tracking::DetectionBox> infer(const cv::Mat&) override {
    if (fail) throw std::runtime_error("inference failed");
    return boxes;
  }
  std::vector<face_tracking::DetectionBox> boxes;
  bool fail{};
};

face_tracking::FrameEvent frame(std::uint64_t sequence) {
  cv::Mat image = cv::Mat::zeros(4, 4, CV_8UC3);
  std::vector<std::uint8_t> jpeg;
  cv::imencode(".jpg", image, jpeg);
  return {.jpeg = std::move(jpeg), .metadata = {.source_instance_id = "camera", .sequence = sequence, .captured_at_unix_ns = 10, .width = 4, .height = 4, .capture_format = "MJPG", .jpeg_quality = 80}};
}
}

TEST(DetectorService, LatestSlotRejectsDuplicateAndOverwrites) {
  face_tracking::detector::internal::LatestFrameSlot slot;
  EXPECT_TRUE(slot.update(frame(1)));
  EXPECT_FALSE(slot.update(frame(1)));
  EXPECT_TRUE(slot.update(frame(2)));
  EXPECT_EQ(slot.take()->metadata.sequence, 2U);
  EXPECT_FALSE(slot.take().has_value());
  EXPECT_EQ(slot.dropped_frames(), 1U);

  EXPECT_TRUE(slot.update(frame(3)));
  EXPECT_EQ(slot.take()->metadata.sequence, 3U);
  EXPECT_EQ(slot.dropped_frames(), 1U);
}

TEST(DetectorService, LoopPublishesEmptyDetection) {
  FakeTransport transport;
  FakeEngine engine;
  face_tracking::detector::internal::DetectorLoop loop(transport, engine, 5);
  EXPECT_TRUE(loop.process_if_due(frame(1), std::chrono::steady_clock::time_point{}));
  ASSERT_EQ(transport.detections.size(), 1U);
  EXPECT_TRUE(transport.detections.front().boxes.empty());
}

TEST(DetectorService, LoopProcessesEachFrameOnlyOnce) {
  FakeTransport transport;
  FakeEngine engine;
  face_tracking::detector::internal::DetectorLoop loop(transport, engine, 5);
  const auto start = std::chrono::steady_clock::time_point{};
  EXPECT_TRUE(loop.process_if_due(frame(1), start));
  EXPECT_FALSE(loop.process_if_due(frame(1), start + std::chrono::milliseconds(200)));
  EXPECT_EQ(transport.detections.size(), 1U);
}

TEST(DetectorService, LoopCountsJpegDecodeFailure) {
  FakeTransport transport;
  FakeEngine engine;
  face_tracking::detector::internal::DetectorLoop loop(transport, engine, 5);
  auto invalid = frame(1);
  invalid.jpeg = {1, 2, 3};
  EXPECT_FALSE(loop.process_if_due(invalid, std::chrono::steady_clock::time_point{}));
  EXPECT_EQ(loop.decode_errors(), 1U);
  EXPECT_TRUE(transport.detections.empty());
}

TEST(DetectorService, LoopCanRecoverAfterInferenceException) {
  FakeTransport transport;
  FakeEngine engine;
  face_tracking::detector::internal::DetectorLoop loop(transport, engine, 5);
  const auto start = std::chrono::steady_clock::time_point{};
  engine.fail = true;
  EXPECT_THROW(loop.process_if_due(frame(1), start), std::runtime_error);
  engine.fail = false;
  EXPECT_TRUE(loop.process_if_due(frame(2), start + std::chrono::milliseconds(200)));
  ASSERT_EQ(transport.detections.size(), 1U);
  EXPECT_EQ(transport.detections.front().sequence, 2U);
}

TEST(OpenCvYolo, LetterboxAndPostprocessRestoreOriginalPixelsAndApplyNms) {
  const auto transform = face_tracking::detector::internal::calculate_letterbox(1280, 720, 640);
  EXPECT_FLOAT_EQ(transform.scale, 0.5F);
  EXPECT_EQ(transform.resized_width, 640);
  EXPECT_EQ(transform.resized_height, 360);
  EXPECT_EQ(transform.pad_x, 0);
  EXPECT_EQ(transform.pad_y, 140);

  const int sizes[]{1, 5, 3};
  cv::Mat raw(3, sizes, CV_32F);
  const std::array<float, 15> values{
      320, 322, 100,
      320, 322, 200,
      100, 100, 50,
      100, 100, 50,
      0.9F, 0.8F, 0.7F,
  };
  std::copy(values.begin(), values.end(), raw.ptr<float>());

  const auto boxes = face_tracking::detector::internal::postprocess_yolo(
      raw, 1280, 720, transform, 0.5F, 0.45F);
  ASSERT_EQ(boxes.size(), 2U);
  EXPECT_FLOAT_EQ(boxes[0].x, 540);
  EXPECT_FLOAT_EQ(boxes[0].y, 260);
  EXPECT_FLOAT_EQ(boxes[0].width, 200);
  EXPECT_FLOAT_EQ(boxes[0].height, 200);
  EXPECT_FLOAT_EQ(boxes[0].confidence, 0.9F);
  EXPECT_FLOAT_EQ(boxes[1].x, 150);
  EXPECT_FLOAT_EQ(boxes[1].y, 70);
  EXPECT_FLOAT_EQ(boxes[1].width, 100);
  EXPECT_FLOAT_EQ(boxes[1].height, 100);
}
