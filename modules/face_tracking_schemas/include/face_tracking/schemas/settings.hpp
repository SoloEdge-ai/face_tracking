#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace face_tracking {

struct MiddlewareSettings {
  std::string adapter{"zenoh"};
  std::string connect;
  std::string key_prefix;
};

struct CameraSettings {
  std::string device_path;
  int width{};
  int height{};
  int capture_fps{};
  int publish_hz{};
  int jpeg_quality{};
  double reconnect_seconds{};
};

struct DetectorSettings {
  std::string model_path;
  int inference_hz{};
  int image_size{};
  float confidence{};
  float iou{};
};

struct HmiSettings {
  std::string host;
  std::uint16_t port{};
  int stale_after_ms{};
  int offline_after_ms{};
};

struct Settings {
  std::string device_id;
  MiddlewareSettings middleware;
  CameraSettings camera;
  DetectorSettings detector;
  HmiSettings hmi;

  [[nodiscard]] std::string key(std::string_view suffix) const;
  [[nodiscard]] std::string camera_image_key() const;
  [[nodiscard]] std::string camera_status_key() const;
  [[nodiscard]] std::string camera_liveliness_key() const;
  [[nodiscard]] std::string detections_key() const;
  [[nodiscard]] std::string detector_status_key() const;
  [[nodiscard]] std::string detector_liveliness_key() const;
};

Settings load_settings(const std::filesystem::path& path);

}  // namespace face_tracking
