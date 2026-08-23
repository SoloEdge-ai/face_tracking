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
  std::uint64_t track_id{};
};

struct DetectionResult {
  std::uint32_t schema_version{kSchemaVersion};
  std::string source_instance_id;
  std::string tracker_instance_id;
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

enum class TrackingState { no_target, tracking, lost };
enum class ControllerDecision { applied, deadband, no_target, lost, stale, duplicate, out_of_order };
enum class PixelCenterControllerState { starting, active, holding, error, stopped };

struct SelectedTargetObservation {
  std::uint32_t schema_version{kSchemaVersion};
  std::string source_instance_id;
  std::string tracker_instance_id;
  std::uint64_t sequence{};
  std::int64_t captured_at_unix_ns{};
  std::uint64_t selected_track_id{};
  float target_center_x{};
  float target_center_y{};
  std::uint32_t image_width{};
  std::uint32_t image_height{};
  TrackingState tracking_state{TrackingState::no_target};
};

struct PanTiltDelta {
  std::uint32_t schema_version{kSchemaVersion};
  std::string source_instance_id;
  std::string tracker_instance_id;
  std::uint64_t sequence{};
  std::int64_t captured_at_unix_ns{};
  std::int64_t computed_at_unix_ns{};
  std::uint64_t selected_track_id{};
  float delta_pan_deg{};
  float delta_tilt_deg{};
  ControllerDecision reason{ControllerDecision::no_target};
};

struct PixelCenterControllerStatus {
  std::uint32_t schema_version{kSchemaVersion};
  PixelCenterControllerState state{PixelCenterControllerState::starting};
  double observation_age_ms{};
  float error_x_px{};
  float error_y_px{};
  float last_delta_pan_deg{};
  float last_delta_tilt_deg{};
  std::uint64_t processed_observations{};
  std::uint64_t stale_observations{};
  std::uint64_t duplicate_observations{};
  std::uint64_t out_of_order_observations{};
  std::optional<std::string> last_rejection_reason;
};

void validate(const FrameMetadata& metadata);
void validate(const DetectionResult& result);
void validate(const SelectedTargetObservation& observation);
void validate(const PanTiltDelta& command);

}  // namespace face_tracking
