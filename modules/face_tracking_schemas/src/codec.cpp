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

wire::TrackingState to_wire(TrackingState value) {
  switch (value) {
    case TrackingState::no_target: return wire::TRACKING_STATE_NO_TARGET;
    case TrackingState::tracking: return wire::TRACKING_STATE_TRACKING;
    case TrackingState::lost: return wire::TRACKING_STATE_LOST;
  }
  throw std::invalid_argument("invalid tracking state");
}

TrackingState from_wire(wire::TrackingState value) {
  switch (value) {
    case wire::TRACKING_STATE_NO_TARGET: return TrackingState::no_target;
    case wire::TRACKING_STATE_TRACKING: return TrackingState::tracking;
    case wire::TRACKING_STATE_LOST: return TrackingState::lost;
    default: throw std::invalid_argument("invalid tracking state");
  }
}

wire::ControllerDecision to_wire(ControllerDecision value) {
  switch (value) {
    case ControllerDecision::applied: return wire::CONTROLLER_DECISION_APPLIED;
    case ControllerDecision::deadband: return wire::CONTROLLER_DECISION_DEADBAND;
    case ControllerDecision::no_target: return wire::CONTROLLER_DECISION_NO_TARGET;
    case ControllerDecision::lost: return wire::CONTROLLER_DECISION_LOST;
    case ControllerDecision::stale: return wire::CONTROLLER_DECISION_STALE;
  }
  throw std::invalid_argument("invalid controller decision");
}

ControllerDecision from_wire(wire::ControllerDecision value) {
  switch (value) {
    case wire::CONTROLLER_DECISION_APPLIED: return ControllerDecision::applied;
    case wire::CONTROLLER_DECISION_DEADBAND: return ControllerDecision::deadband;
    case wire::CONTROLLER_DECISION_NO_TARGET: return ControllerDecision::no_target;
    case wire::CONTROLLER_DECISION_LOST: return ControllerDecision::lost;
    case wire::CONTROLLER_DECISION_STALE: return ControllerDecision::stale;
    default: throw std::invalid_argument("invalid controller decision");
  }
}

wire::PixelCenterControllerState to_wire(PixelCenterControllerState value) {
  switch (value) {
    case PixelCenterControllerState::starting: return wire::PIXEL_CENTER_CONTROLLER_STATE_STARTING;
    case PixelCenterControllerState::active: return wire::PIXEL_CENTER_CONTROLLER_STATE_ACTIVE;
    case PixelCenterControllerState::holding: return wire::PIXEL_CENTER_CONTROLLER_STATE_HOLDING;
    case PixelCenterControllerState::error: return wire::PIXEL_CENTER_CONTROLLER_STATE_ERROR;
    case PixelCenterControllerState::stopped: return wire::PIXEL_CENTER_CONTROLLER_STATE_STOPPED;
  }
  throw std::invalid_argument("invalid controller state");
}

PixelCenterControllerState from_wire(wire::PixelCenterControllerState value) {
  switch (value) {
    case wire::PIXEL_CENTER_CONTROLLER_STATE_STARTING: return PixelCenterControllerState::starting;
    case wire::PIXEL_CENTER_CONTROLLER_STATE_ACTIVE: return PixelCenterControllerState::active;
    case wire::PIXEL_CENTER_CONTROLLER_STATE_HOLDING: return PixelCenterControllerState::holding;
    case wire::PIXEL_CENTER_CONTROLLER_STATE_ERROR: return PixelCenterControllerState::error;
    case wire::PIXEL_CENTER_CONTROLLER_STATE_STOPPED: return PixelCenterControllerState::stopped;
    default: throw std::invalid_argument("invalid controller state");
  }
}
}

std::vector<std::uint8_t> encode(const FrameMetadata& value) {
  validate(value);
  wire::FrameMetadata message;
  message.set_schema_version(value.schema_version);
  message.set_source_instance_id(value.source_instance_id);
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
  message.set_tracker_instance_id(value.tracker_instance_id);
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

std::vector<std::uint8_t> encode(const SelectedTargetObservation& value) {
  validate(value);
  wire::SelectedTargetObservation message;
  message.set_schema_version(value.schema_version);
  message.set_source_instance_id(value.source_instance_id);
  message.set_tracker_instance_id(value.tracker_instance_id);
  message.set_sequence(value.sequence);
  message.set_captured_at_unix_ns(value.captured_at_unix_ns);
  message.set_selected_track_id(value.selected_track_id);
  message.set_target_center_x(value.target_center_x);
  message.set_target_center_y(value.target_center_y);
  message.set_image_width(value.image_width);
  message.set_image_height(value.image_height);
  message.set_tracking_state(to_wire(value.tracking_state));
  return serialize(message);
}

SelectedTargetObservation decode_selected_target(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::SelectedTargetObservation>(bytes);
  SelectedTargetObservation value{
      .schema_version = message.schema_version(),
      .source_instance_id = message.source_instance_id(),
      .tracker_instance_id = message.tracker_instance_id(),
      .sequence = message.sequence(),
      .captured_at_unix_ns = message.captured_at_unix_ns(),
      .selected_track_id = message.selected_track_id(),
      .target_center_x = message.target_center_x(),
      .target_center_y = message.target_center_y(),
      .image_width = message.image_width(),
      .image_height = message.image_height(),
      .tracking_state = from_wire(message.tracking_state()),
  };
  validate(value);
  return value;
}

std::vector<std::uint8_t> encode(const PanTiltDelta& value) {
  validate(value);
  wire::PanTiltDelta message;
  message.set_schema_version(value.schema_version);
  message.set_source_instance_id(value.source_instance_id);
  message.set_tracker_instance_id(value.tracker_instance_id);
  message.set_sequence(value.sequence);
  message.set_captured_at_unix_ns(value.captured_at_unix_ns);
  message.set_computed_at_unix_ns(value.computed_at_unix_ns);
  message.set_selected_track_id(value.selected_track_id);
  message.set_delta_pan_deg(value.delta_pan_deg);
  message.set_delta_tilt_deg(value.delta_tilt_deg);
  message.set_reason(to_wire(value.reason));
  return serialize(message);
}

PanTiltDelta decode_pan_tilt_delta(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::PanTiltDelta>(bytes);
  PanTiltDelta value{
      .schema_version = message.schema_version(),
      .source_instance_id = message.source_instance_id(),
      .tracker_instance_id = message.tracker_instance_id(),
      .sequence = message.sequence(),
      .captured_at_unix_ns = message.captured_at_unix_ns(),
      .computed_at_unix_ns = message.computed_at_unix_ns(),
      .selected_track_id = message.selected_track_id(),
      .delta_pan_deg = message.delta_pan_deg(),
      .delta_tilt_deg = message.delta_tilt_deg(),
      .reason = from_wire(message.reason()),
  };
  validate(value);
  return value;
}

std::vector<std::uint8_t> encode(const PixelCenterControllerStatus& value) {
  wire::PixelCenterControllerStatus message;
  message.set_schema_version(value.schema_version);
  message.set_state(to_wire(value.state));
  message.set_observation_age_ms(value.observation_age_ms);
  message.set_error_x_px(value.error_x_px);
  message.set_error_y_px(value.error_y_px);
  message.set_last_delta_pan_deg(value.last_delta_pan_deg);
  message.set_last_delta_tilt_deg(value.last_delta_tilt_deg);
  message.set_processed_observations(value.processed_observations);
  message.set_stale_observations(value.stale_observations);
  message.set_duplicate_observations(value.duplicate_observations);
  message.set_out_of_order_observations(value.out_of_order_observations);
  if (value.last_rejection_reason) message.set_last_rejection_reason(*value.last_rejection_reason);
  return serialize(message);
}

PixelCenterControllerStatus decode_pixel_center_controller_status(std::span<const std::uint8_t> bytes) {
  const auto message = parse<wire::PixelCenterControllerStatus>(bytes);
  if (message.schema_version() != kSchemaVersion) throw std::invalid_argument("unsupported schema version");
  return {
      .schema_version = message.schema_version(),
      .state = from_wire(message.state()),
      .observation_age_ms = message.observation_age_ms(),
      .error_x_px = message.error_x_px(),
      .error_y_px = message.error_y_px(),
      .last_delta_pan_deg = message.last_delta_pan_deg(),
      .last_delta_tilt_deg = message.last_delta_tilt_deg(),
      .processed_observations = message.processed_observations(),
      .stale_observations = message.stale_observations(),
      .duplicate_observations = message.duplicate_observations(),
      .out_of_order_observations = message.out_of_order_observations(),
      .last_rejection_reason = message.has_last_rejection_reason() ? std::optional<std::string>(message.last_rejection_reason()) : std::nullopt,
  };
}

}  // namespace face_tracking::codec
