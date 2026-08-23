#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "face_tracking/schemas/types.hpp"

namespace face_tracking::codec {

std::vector<std::uint8_t> encode(const FrameMetadata& value);
std::vector<std::uint8_t> encode(const DetectionResult& value);
std::vector<std::uint8_t> encode(const CameraStatus& value);
std::vector<std::uint8_t> encode(const DetectorStatus& value);
std::vector<std::uint8_t> encode(const SelectedTargetObservation& value);
std::vector<std::uint8_t> encode(const PanTiltDelta& value);
std::vector<std::uint8_t> encode(const PixelCenterControllerStatus& value);

FrameMetadata decode_frame_metadata(std::span<const std::uint8_t> bytes);
DetectionResult decode_detection_result(std::span<const std::uint8_t> bytes);
CameraStatus decode_camera_status(std::span<const std::uint8_t> bytes);
DetectorStatus decode_detector_status(std::span<const std::uint8_t> bytes);
SelectedTargetObservation decode_selected_target(std::span<const std::uint8_t> bytes);
PanTiltDelta decode_pan_tilt_delta(std::span<const std::uint8_t> bytes);
PixelCenterControllerStatus decode_pixel_center_controller_status(std::span<const std::uint8_t> bytes);

}  // namespace face_tracking::codec
