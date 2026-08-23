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
}

Settings load_settings(const std::filesystem::path& path) {
  const auto root = YAML::LoadFile(path.string());
  const auto common = root["common"];
  const auto middleware = root["middleware"];
  const auto camera = root["camera"];
  const auto detector = root["detector"];
  const auto hmi = root["hmi"];
  Settings settings{
      .device_id = required<std::string>(common, "device_id"),
      .middleware = {.adapter = required<std::string>(middleware, "adapter"), .connect = required<std::string>(middleware, "connect"), .key_prefix = required<std::string>(middleware, "key_prefix")},
      .camera = {.device_path = required<std::string>(camera, "device_path"), .width = required<int>(camera, "width"), .height = required<int>(camera, "height"), .capture_fps = required<int>(camera, "capture_fps"), .publish_hz = required<int>(camera, "publish_hz"), .jpeg_quality = required<int>(camera, "jpeg_quality"), .reconnect_seconds = required<double>(camera, "reconnect_seconds")},
      .detector = {.model_path = required<std::string>(detector, "model_path"), .inference_hz = required<int>(detector, "inference_hz"), .image_size = required<int>(detector, "image_size"), .confidence = required<float>(detector, "confidence"), .iou = required<float>(detector, "iou")},
      .hmi = {.host = required<std::string>(hmi, "host"), .port = required<std::uint16_t>(hmi, "port"), .stale_after_ms = required<int>(hmi, "stale_after_ms"), .offline_after_ms = required<int>(hmi, "offline_after_ms")},
  };
  if (const char* value = std::getenv("FACE_TRACKING_DEVICE_ID")) settings.device_id = value;
  if (settings.middleware.adapter != "zenoh") throw std::runtime_error("only the zenoh adapter is built in this release");
  if (settings.device_id.empty() || settings.middleware.connect.empty() || settings.middleware.key_prefix.empty()) {
    throw std::runtime_error("invalid common or middleware configuration");
  }
  if (settings.camera.device_path.empty() || settings.camera.width <= 0 || settings.camera.height <= 0 ||
      settings.camera.capture_fps <= 0 || settings.camera.publish_hz <= 0 ||
      settings.camera.jpeg_quality < 1 || settings.camera.jpeg_quality > 100 ||
      settings.camera.reconnect_seconds <= 0) {
    throw std::runtime_error("invalid camera configuration");
  }
  if (settings.detector.model_path.empty() || settings.detector.inference_hz <= 0 ||
      settings.detector.image_size <= 0 || settings.detector.confidence < 0 ||
      settings.detector.confidence > 1 || settings.detector.iou < 0 || settings.detector.iou > 1) {
    throw std::runtime_error("invalid detector configuration");
  }
  if (settings.hmi.port == 0 || settings.hmi.stale_after_ms <= 0 ||
      settings.hmi.offline_after_ms <= settings.hmi.stale_after_ms) {
    throw std::runtime_error("invalid HMI configuration");
  }
  return settings;
}

std::string Settings::key(std::string_view suffix) const { return middleware.key_prefix + "/" + device_id + "/" + std::string(suffix); }
std::string Settings::camera_image_key() const { return key("camera/image"); }
std::string Settings::camera_status_key() const { return key("camera/status"); }
std::string Settings::camera_liveliness_key() const { return key("liveliness/camera"); }
std::string Settings::detections_key() const { return key("detections"); }
std::string Settings::detector_status_key() const { return key("diagnostics/detector"); }
std::string Settings::detector_liveliness_key() const { return key("liveliness/detector"); }

}  // namespace face_tracking
