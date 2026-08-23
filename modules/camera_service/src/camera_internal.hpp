#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "face_tracking/camera/camera_service.hpp"

namespace face_tracking::camera::internal {

struct CapturedFrame {
  cv::Mat bgr;
  std::int64_t captured_at_unix_ns{};
};

class LatestFrameSlot {
 public:
  void update(cv::Mat bgr, std::int64_t captured_at_unix_ns);
  [[nodiscard]] std::optional<CapturedFrame> snapshot() const;
  [[nodiscard]] std::uint64_t captured_frames() const;

 private:
  mutable std::mutex mutex_;
  std::optional<CapturedFrame> frame_;
  std::uint64_t captured_frames_{};
};

class PublishGate {
 public:
  PublishGate(OutputPort& output, std::string source_instance_id, const CameraSettings& settings);
  void accept(std::vector<std::uint8_t> jpeg, std::int64_t captured_at_unix_ns);
  bool publish_if_due(std::chrono::steady_clock::time_point now);
  [[nodiscard]] bool due(std::chrono::steady_clock::time_point now) const;
  [[nodiscard]] std::uint64_t published_frames() const;

 private:
  OutputPort& output_;
  std::string source_instance_id_;
  CameraSettings settings_;
  std::vector<std::uint8_t> latest_jpeg_;
  std::int64_t latest_capture_ns_{};
  std::int64_t last_published_capture_ns_{};
  std::chrono::steady_clock::time_point next_publish_at_{};
  std::uint64_t sequence_{};
  std::uint64_t published_frames_{};
};

std::string make_instance_id();

}  // namespace face_tracking::camera::internal
