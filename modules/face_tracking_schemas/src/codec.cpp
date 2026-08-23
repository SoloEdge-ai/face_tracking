#include "face_tracking/schemas/codec.hpp"

#include <stdexcept>
#include <string>
#include "face_tracking_v2.pb.h"

namespace face_tracking::codec {
namespace wire = face_tracking::wire::v2;

namespace {
template <typename Message>
std::vector<std::uint8_t> serialize(const Message& message) {
  std::string data;
  if (!message.SerializeToString(&data)) throw std::runtime_error("protobuf serialization failed");
  return {data.begin(), data.end()};
}

template <typename Message>
Message parse(std::span<const std::uint8_t> bytes) {
  Message message;
  if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) throw std::invalid_argument("invalid protobuf payload");
  return message;
}

wire::CameraState to_wire(CameraState value) {
  switch (value) {
    case CameraState::starting: return wire::CAMERA_STATE_STARTING;
    case CameraState::streaming: return wire::CAMERA_STATE_STREAMING;
    case CameraState::reconnecting: return wire::CAMERA_STATE_RECONNECTING;
    case CameraState::error: return wire::CAMERA_STATE_ERROR;
    case CameraState::stopped: return wire::CAMERA_STATE_STOPPED;
  }
  throw std::invalid_argument("invalid camera state");
}

CameraState from_wire(wire::CameraState value) {
  switch (value) {
    case wire::CAMERA_STATE_STARTING: return CameraState::starting;
    case wire::CAMERA_STATE_STREAMING: return CameraState::streaming;
    case wire::CAMERA_STATE_RECONNECTING: return CameraState::reconnecting;
    case wire::CAMERA_STATE_ERROR: return CameraState::error;
    case wire::CAMERA_STATE_STOPPED: return CameraState::stopped;
    default: throw std::invalid_argument("invalid camera state");
  }
}

wire::DetectorState to_wire(DetectorState value) {
  switch (value) {
    case DetectorState::starting: return wire::DETECTOR_STATE_STARTING;
    case DetectorState::streaming: return wire::DETECTOR_STATE_STREAMING;
    case DetectorState::error: return wire::DETECTOR_STATE_ERROR;
    case DetectorState::stopped: return wire::DETECTOR_STATE_STOPPED;
  }
  throw std::invalid_argument("invalid detector state");
}

DetectorState from_wire(wire::DetectorState value) {
  switch (value) {
    case wire::DETECTOR_STATE_STARTING: return DetectorState::starting;
    case wire::DETECTOR_STATE_STREAMING: return DetectorState::streaming;
    case wire::DETECTOR_STATE_ERROR: return DetectorState::error;
    case wire::DETECTOR_STATE_STOPPED: return DetectorState::stopped;
    default: throw std::invalid_argument("invalid detector state");
  }
}
}

std::vector<std::uint8_t> encode(const FrameMetadata& value) {
  validate(value);
  wire::FrameMetadata message;
  message.set_schema_version(value.schema_version);
  message.set_source_instance_id(value.source_instance_id);
  message.set_tracker_instance_id(value.tracker_instance_id);
  message.set_sequence(value.sequence);
  message.set_captured_at_unix_ns(value.captured_at_unix_ns);
  message.set_width(value.width);
  message.set_height(value.height);
  message.set_capture_format(value.capture_format);
  message.set_jpeg_quality(value.jpeg_quality);
  return serialize(message);
}

FrameMetadata decode_frame_metadata(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::FrameMetadata>(bytes);
  FrameMetadata value{.schema_version = message.schema_version(), .source_instance_id = message.source_instance_id(), .sequence = message.sequence(), .captured_at_unix_ns = message.captured_at_unix_ns(), .width = message.width(), .height = message.height(), .capture_format = message.capture_format(), .jpeg_quality = message.jpeg_quality()};
  validate(value);
  return value;
}

std::vector<std::uint8_t> encode(const DetectionResult& value) {
  validate(value);
  wire::DetectionResult message;
  message.set_schema_version(value.schema_version);
  message.set_source_instance_id(value.source_instance_id);
  message.set_sequence(value.sequence);
  message.set_captured_at_unix_ns(value.captured_at_unix_ns);
  message.set_image_width(value.image_width);
  message.set_image_height(value.image_height);
  message.set_inference_ms(value.inference_ms);
  for (const auto& box : value.boxes) {
    auto* output = message.add_boxes();
    output->set_x(box.x);
    output->set_y(box.y);
    output->set_width(box.width);
    output->set_height(box.height);
    output->set_confidence(box.confidence);
    output->set_track_id(box.track_id);
  }
  return serialize(message);
}

DetectionResult decode_detection_result(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::DetectionResult>(bytes);
  DetectionResult value{.schema_version = message.schema_version(), .source_instance_id = message.source_instance_id(), .tracker_instance_id = message.tracker_instance_id(), .sequence = message.sequence(), .captured_at_unix_ns = message.captured_at_unix_ns(), .image_width = message.image_width(), .image_height = message.image_height(), .inference_ms = message.inference_ms(), .boxes = {}};
  value.boxes.reserve(message.boxes_size());
  for (const auto& box : message.boxes()) value.boxes.push_back({box.x(), box.y(), box.width(), box.height(), box.confidence(), box.track_id()});
  validate(value);
  return value;
}

std::vector<std::uint8_t> encode(const CameraStatus& value) {
  wire::CameraStatus message;
  message.set_schema_version(value.schema_version);
  message.set_state(to_wire(value.state));
  message.set_capture_fps(value.capture_fps);
  message.set_publish_fps(value.publish_fps);
  message.set_captured_frames(value.captured_frames);
  message.set_published_frames(value.published_frames);
  message.set_dropped_frames(value.dropped_frames);
  message.set_device_path(value.device_path);
  if (value.last_error) message.set_last_error(*value.last_error);
  return serialize(message);
}

CameraStatus decode_camera_status(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::CameraStatus>(bytes);
  if (message.schema_version() != kSchemaVersion) throw std::invalid_argument("unsupported schema version");
  return {.schema_version = message.schema_version(), .state = from_wire(message.state()), .capture_fps = message.capture_fps(), .publish_fps = message.publish_fps(), .captured_frames = message.captured_frames(), .published_frames = message.published_frames(), .dropped_frames = message.dropped_frames(), .device_path = message.device_path(), .last_error = message.has_last_error() ? std::optional<std::string>(message.last_error()) : std::nullopt};
}

std::vector<std::uint8_t> encode(const DetectorStatus& value) {
  wire::DetectorStatus message;
  message.set_schema_version(value.schema_version);
  message.set_state(to_wire(value.state));
  message.set_inference_fps(value.inference_fps);
  message.set_processed_frames(value.processed_frames);
  message.set_dropped_frames(value.dropped_frames);
  message.set_decode_errors(value.decode_errors);
  message.set_inference_errors(value.inference_errors);
  if (value.last_error) message.set_last_error(*value.last_error);
  return serialize(message);
}

DetectorStatus decode_detector_status(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::DetectorStatus>(bytes);
  if (message.schema_version() != kSchemaVersion) throw std::invalid_argument("unsupported schema version");
  return {.schema_version = message.schema_version(), .state = from_wire(message.state()), .inference_fps = message.inference_fps(), .processed_frames = message.processed_frames(), .dropped_frames = message.dropped_frames(), .decode_errors = message.decode_errors(), .inference_errors = message.inference_errors(), .last_error = message.has_last_error() ? std::optional<std::string>(message.last_error()) : std::nullopt};
}

}  // namespace face_tracking::codec
