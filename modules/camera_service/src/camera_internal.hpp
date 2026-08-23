#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
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

class CameraDevice {
 public:
  virtual ~CameraDevice() = default;
  [[nodiscard]] virtual bool is_open() const = 0;
  virtual bool open(const std::string& path) = 0;
  virtual void configure(const CameraSettings& settings) = 0;
  virtual bool read(cv::Mat& frame) = 0;
  virtual void release() = 0;
};

using CameraStateHandler =
    std::function<void(CameraState, std::optional<std::string>)>;

bool capture_once(CameraDevice& device, const CameraSettings& settings, LatestFrameSlot& slot,
                  const CameraStateHandler& set_state, std::int64_t captured_at_unix_ns);

std::string make_instance_id();
void recover_jpeg_error(CameraState& state, std::optional<std::string>& last_error);

}  // namespace face_tracking::camera::internal
