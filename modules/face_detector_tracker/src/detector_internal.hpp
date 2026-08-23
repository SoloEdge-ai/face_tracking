#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include <opencv2/core/mat.hpp>

#include "face_tracking/detector/detector_service.hpp"
#include "face_tracking/detector/face_tracker.hpp"

namespace face_tracking::detector::internal {

class InferenceEngine {
 public:
  virtual ~InferenceEngine() = default;
  virtual std::vector<DetectionBox> infer(const cv::Mat& image) = 0;
};

class LatestFrameSlot {
 public:
  bool update(FrameEvent frame);
  [[nodiscard]] std::optional<FrameEvent> take();
  [[nodiscard]] std::uint64_t dropped_frames() const;

 private:
  mutable std::mutex mutex_;
  std::optional<FrameEvent> frame_;
  std::optional<std::pair<std::string, std::uint64_t>> last_key_;
  std::uint64_t dropped_frames_{};
};

class DetectorLoop {
 public:
  DetectorLoop(TransportPort& transport, InferenceEngine& engine, int inference_hz,
               TrackerSettings tracker_settings = {});
  [[nodiscard]] bool due(std::chrono::steady_clock::time_point now) const;
  bool process_if_due(const std::optional<FrameEvent>& frame, std::chrono::steady_clock::time_point now);
  [[nodiscard]] std::uint64_t processed_frames() const;
  [[nodiscard]] std::uint64_t decode_errors() const;

 private:
  TransportPort& transport_;
  InferenceEngine& engine_;
  int inference_hz_;
  FaceTracker tracker_;
  std::chrono::steady_clock::time_point next_inference_at_{};
  std::optional<std::pair<std::string, std::uint64_t>> last_processed_key_;
  std::optional<std::string> active_source_instance_id_;
  std::uint64_t processed_frames_{};
  std::uint64_t decode_errors_{};
};

class OpenCvYoloEngine final : public InferenceEngine {
 public:
  explicit OpenCvYoloEngine(const DetectorSettings& settings);
  ~OpenCvYoloEngine() override;
  std::vector<DetectionBox> infer(const cv::Mat& image) override;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

struct LetterboxTransform {
  float scale{};
  int resized_width{};
  int resized_height{};
  int pad_x{};
  int pad_y{};
};

LetterboxTransform calculate_letterbox(int image_width, int image_height, int input_size);
std::vector<DetectionBox> postprocess_yolo(
    const cv::Mat& raw, int image_width, int image_height, const LetterboxTransform& transform,
    float confidence_threshold, float iou_threshold);

}  // namespace face_tracking::detector::internal
