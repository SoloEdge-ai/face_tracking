#include "face_tracking/schemas/settings.hpp"

#include <cstdlib>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace face_tracking {
namespace {
template <typename T>
T required(const YAML::Node& node, const char* key) {
  if (!node[key]) throw std::runtime_error(std::string("missing config key: ") + key);
  return node[key].as<T>();
}

TransportSettings load_transport(const YAML::Node& root) {
  const auto common = root["common"];
  const auto middleware = root["middleware"];
  TransportSettings settings{
      .device_id = required<std::string>(common, "device_id"),
      .middleware = {.adapter = required<std::string>(middleware, "adapter"), .connect = required<std::string>(middleware, "connect"), .key_prefix = required<std::string>(middleware, "key_prefix")},
  };
  if (const char* value = std::getenv("FACE_TRACKING_DEVICE_ID")) settings.device_id = value;
  if (settings.device_id.empty() || settings.middleware.connect.empty() || settings.middleware.key_prefix.empty()) {
    throw std::runtime_error("invalid common or middleware configuration");
  }
  return settings;
}

ServoAxisSettings load_servo_axis(const YAML::Node& node) {
  return {
      .gpio = required<int>(node, "gpio"),
      .rated_max_deg = required<float>(node, "rated_max_deg"),
      .min_deg = required<float>(node, "min_deg"),
      .max_deg = required<float>(node, "max_deg"),
      .home_deg = required<float>(node, "home_deg"),
      .min_pulse_us = required<int>(node, "min_pulse_us"),
      .max_pulse_us = required<int>(node, "max_pulse_us"),
      .invert = required<bool>(node, "invert"),
  };
}

void validate_servo_axis(const ServoAxisSettings& axis) {
  if (axis.gpio < 0 || axis.rated_max_deg <= 0 || axis.min_deg < 0 ||
      axis.min_deg >= axis.max_deg || axis.max_deg > axis.rated_max_deg ||
      axis.home_deg < axis.min_deg || axis.home_deg > axis.max_deg || axis.min_pulse_us <= 0 ||
      axis.min_pulse_us >= axis.max_pulse_us) {
    throw std::runtime_error("invalid servo axis configuration");
  }
}
}

CameraProcessSettings load_camera_process_settings(const std::filesystem::path& path) {
  const auto root = YAML::LoadFile(path.string());
  const auto camera = root["camera"];
  CameraProcessSettings settings{
      .transport = load_transport(root),
      .camera = {.device_path = required<std::string>(camera, "device_path"), .width = required<int>(camera, "width"), .height = required<int>(camera, "height"), .capture_fps = required<int>(camera, "capture_fps"), .publish_hz = required<int>(camera, "publish_hz"), .jpeg_quality = required<int>(camera, "jpeg_quality"), .reconnect_seconds = required<double>(camera, "reconnect_seconds")},
  };
  if (settings.camera.device_path.empty() || settings.camera.width <= 0 || settings.camera.height <= 0 ||
      settings.camera.capture_fps <= 0 || settings.camera.publish_hz <= 0 ||
      settings.camera.jpeg_quality < 1 || settings.camera.jpeg_quality > 100 ||
      settings.camera.reconnect_seconds <= 0) {
    throw std::runtime_error("invalid camera configuration");
  }
  return settings;
}

DetectorProcessSettings load_detector_process_settings(const std::filesystem::path& path) {
  const auto root = YAML::LoadFile(path.string());
  const auto detector = root["detector"];
  const auto tracker = detector["tracker"];
  DetectorProcessSettings settings{
      .transport = load_transport(root),
      .detector = {.model_path = required<std::string>(detector, "model_path"), .inference_hz = required<int>(detector, "inference_hz"), .image_size = required<int>(detector, "image_size"), .confidence = required<float>(detector, "confidence"), .iou = required<float>(detector, "iou"), .tracker = {.retention_ms = required<int>(tracker, "retention_ms"), .min_match_iou = required<float>(tracker, "min_match_iou"), .max_center_distance_ratio = required<float>(tracker, "max_center_distance_ratio")}},
  };
  if (settings.detector.model_path.empty() || settings.detector.inference_hz <= 0 ||
      settings.detector.image_size <= 0 || settings.detector.confidence < 0 ||
      settings.detector.confidence > 1 || settings.detector.iou < 0 || settings.detector.iou > 1 ||
      settings.detector.tracker.retention_ms <= 0 || settings.detector.tracker.min_match_iou < 0 ||
      settings.detector.tracker.min_match_iou > 1 || settings.detector.tracker.max_center_distance_ratio <= 0) {
    throw std::runtime_error("invalid detector configuration");
  }
  return settings;
}

ControllerProcessSettings load_controller_process_settings(const std::filesystem::path& path) {
  const auto root = YAML::LoadFile(path.string());
  const auto controller = root["controller"];
  ControllerProcessSettings settings{
      .transport = load_transport(root),
      .controller = {
          .control_rate_hz = required<int>(controller, "control_rate_hz"),
          .deadband_x_px = required<float>(controller, "deadband_x_px"),
          .deadband_y_px = required<float>(controller, "deadband_y_px"),
          .kp_pan_deg_per_px = required<float>(controller, "kp_pan_deg_per_px"),
          .kp_tilt_deg_per_px = required<float>(controller, "kp_tilt_deg_per_px"),
          .max_pan_step_deg = required<float>(controller, "max_pan_step_deg"),
          .max_tilt_step_deg = required<float>(controller, "max_tilt_step_deg"),
          .observation_timeout_ms = required<int>(controller, "observation_timeout_ms"),
      },
  };
  const auto& value = settings.controller;
  if (value.control_rate_hz <= 0 || value.deadband_x_px < 0 || value.deadband_y_px < 0 ||
      value.kp_pan_deg_per_px < 0 || value.kp_tilt_deg_per_px < 0 ||
      value.max_pan_step_deg <= 0 || value.max_tilt_step_deg <= 0 || value.observation_timeout_ms <= 0) {
    throw std::runtime_error("invalid pixel center controller configuration");
  }
  return settings;
}

ServoProcessSettings load_servo_process_settings(const std::filesystem::path& path) {
  const auto root = YAML::LoadFile(path.string());
  const auto servo = root["servo"];
  ServoProcessSettings settings{
      .transport = load_transport(root),
      .servo = {
          .pwm_chip = required<int>(servo, "pwm_chip"),
          .frequency_hz = required<int>(servo, "frequency_hz"),
          .upstream_timeout_ms = required<int>(servo, "upstream_timeout_ms"),
          .max_input_pan_delta_deg = required<float>(servo, "max_input_pan_delta_deg"),
          .max_input_tilt_delta_deg = required<float>(servo, "max_input_tilt_delta_deg"),
          .tracking_enabled = required<bool>(servo, "tracking_enabled"),
          .pan = load_servo_axis(servo["pan"]),
          .tilt = load_servo_axis(servo["tilt"]),
      },
  };
  validate_servo_axis(settings.servo.pan);
  validate_servo_axis(settings.servo.tilt);
  const auto pulse_fits_period = [&settings](const ServoAxisSettings& axis) {
    return static_cast<long long>(axis.max_pulse_us) * settings.servo.frequency_hz <
           1'000'000LL;
  };
  if (settings.servo.pwm_chip < 0 || settings.servo.frequency_hz <= 0 ||
      settings.servo.upstream_timeout_ms <= 0 ||
      settings.servo.max_input_pan_delta_deg <= 0 ||
      settings.servo.max_input_tilt_delta_deg <= 0 ||
      settings.servo.pan.gpio == settings.servo.tilt.gpio ||
      !pulse_fits_period(settings.servo.pan) || !pulse_fits_period(settings.servo.tilt)) {
    throw std::runtime_error("invalid servo driver configuration");
  }
  return settings;
}

std::string TransportSettings::key(std::string_view suffix) const { return middleware.key_prefix + "/" + device_id + "/" + std::string(suffix); }
std::string TransportSettings::camera_image_key() const { return key("camera/image"); }
std::string TransportSettings::camera_status_key() const { return key("camera/status"); }
std::string TransportSettings::camera_liveliness_key() const { return key("liveliness/camera"); }
std::string TransportSettings::detections_key() const { return key("detections"); }
std::string TransportSettings::detector_status_key() const { return key("diagnostics/detector"); }
std::string TransportSettings::detector_liveliness_key() const { return key("liveliness/detector"); }
std::string TransportSettings::selected_target_key() const { return key("target/selected"); }
std::string TransportSettings::pan_tilt_delta_key() const { return key("pan_tilt/delta_cmd"); }
std::string TransportSettings::controller_status_key() const { return key("diagnostics/pixel_center_controller"); }
std::string TransportSettings::controller_liveliness_key() const { return key("liveliness/pixel_center_controller"); }
std::string TransportSettings::pan_tilt_commanded_state_key() const { return key("pan_tilt/commanded_state"); }
std::string TransportSettings::servo_liveliness_key() const { return key("liveliness/servo_driver"); }

}  // namespace face_tracking
