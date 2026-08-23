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

struct TrackerSettings {
  int retention_ms{1000};
  float min_match_iou{0.1F};
  float max_center_distance_ratio{1.0F};
};

struct DetectorSettings {
  std::string model_path;
  int inference_hz{};
  int image_size{};
  float confidence{};
  float iou{};
  TrackerSettings tracker;
};

struct HmiSettings {
  std::string host;
  std::uint16_t port{};
  int stale_after_ms{};
  int offline_after_ms{};
};

struct PixelCenterControllerSettings {
  int control_rate_hz{};
  float deadband_x_px{};
  float deadband_y_px{};
  float kp_pan_deg_per_px{};
  float kp_tilt_deg_per_px{};
  float max_pan_step_deg{};
  float max_tilt_step_deg{};
  int observation_timeout_ms{};
};

struct ServoAxisSettings {
  int gpio{};
  float rated_max_deg{};
  float min_deg{};
  float max_deg{};
  float home_deg{};
  int min_pulse_us{};
  int max_pulse_us{};
  bool invert{};
};

struct ServoDriverSettings {
  int gpio_chip{};
  int frequency_hz{};
  int upstream_timeout_ms{};
  bool tracking_enabled{};
  ServoAxisSettings pan;
  ServoAxisSettings tilt;
};

struct TransportSettings {
  std::string device_id;
  MiddlewareSettings middleware;

  [[nodiscard]] std::string key(std::string_view suffix) const;
  [[nodiscard]] std::string camera_image_key() const;
  [[nodiscard]] std::string camera_status_key() const;
  [[nodiscard]] std::string camera_liveliness_key() const;
  [[nodiscard]] std::string detections_key() const;
  [[nodiscard]] std::string detector_status_key() const;
  [[nodiscard]] std::string detector_liveliness_key() const;
  [[nodiscard]] std::string selected_target_key() const;
  [[nodiscard]] std::string pan_tilt_delta_key() const;
  [[nodiscard]] std::string controller_status_key() const;
  [[nodiscard]] std::string controller_liveliness_key() const;
  [[nodiscard]] std::string pan_tilt_commanded_state_key() const;
  [[nodiscard]] std::string servo_liveliness_key() const;
};

struct CameraProcessSettings {
  TransportSettings transport;
  CameraSettings camera;
};

struct DetectorProcessSettings {
  TransportSettings transport;
  DetectorSettings detector;
};

struct ControllerProcessSettings {
  TransportSettings transport;
  PixelCenterControllerSettings controller;
};

struct ServoProcessSettings {
  TransportSettings transport;
  ServoDriverSettings servo;
};

CameraProcessSettings load_camera_process_settings(const std::filesystem::path& path);
DetectorProcessSettings load_detector_process_settings(const std::filesystem::path& path);
ControllerProcessSettings load_controller_process_settings(const std::filesystem::path& path);
ServoProcessSettings load_servo_process_settings(const std::filesystem::path& path);

}  // namespace face_tracking
