#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace face_tracking {

inline constexpr std::uint32_t kSchemaVersion = 2;

struct FrameMetadata {
  std::uint32_t schema_version{kSchemaVersion};
  std::string source_instance_id;
  std::uint64_t sequence{};
  std::int64_t captured_at_unix_ns{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::string capture_format;
  std::uint32_t jpeg_quality{};
};

struct FrameEvent {
  std::vector<std::uint8_t> jpeg;
  FrameMetadata metadata;
};

struct DetectionBox {
  float x{};
  float y{};
  float width{};
  float height{};
  float confidence{};
};

struct DetectionResult {
  std::uint32_t schema_version{kSchemaVersion};
  std::string source_instance_id;
  std::uint64_t sequence{};
  std::int64_t captured_at_unix_ns{};
  std::uint32_t image_width{};
  std::uint32_t image_height{};
  double inference_ms{};
  std::vector<DetectionBox> boxes;
};

enum class CameraState { starting, streaming, reconnecting, error, stopped };
enum class DetectorState { starting, streaming, error, stopped };

struct CameraStatus {
  std::uint32_t schema_version{kSchemaVersion};
  CameraState state{CameraState::starting};
  double capture_fps{};
  double publish_fps{};
  std::uint64_t captured_frames{};
  std::uint64_t published_frames{};
  std::uint64_t dropped_frames{};
  std::string device_path;
  std::optional<std::string> last_error;
};

struct DetectorStatus {
  std::uint32_t schema_version{kSchemaVersion};
  DetectorState state{DetectorState::starting};
  double inference_fps{};
  std::uint64_t processed_frames{};
  std::uint64_t dropped_frames{};
  std::uint64_t decode_errors{};
  std::uint64_t inference_errors{};
  std::optional<std::string> last_error;
};

void validate(const FrameMetadata& metadata);
void validate(const DetectionResult& result);

}  // namespace face_tracking
