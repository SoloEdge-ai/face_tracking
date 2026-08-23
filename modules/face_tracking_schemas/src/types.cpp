#include "face_tracking/schemas/types.hpp"

#include <cmath>
#include <stdexcept>

namespace face_tracking {

void validate(const FrameMetadata& metadata) {
  if (metadata.schema_version != kSchemaVersion) throw std::invalid_argument("unsupported schema version");
  if (metadata.source_instance_id.empty()) throw std::invalid_argument("source_instance_id is required");
  if (metadata.captured_at_unix_ns <= 0) throw std::invalid_argument("capture timestamp must be positive");
  if (metadata.width == 0 || metadata.height == 0) throw std::invalid_argument("image dimensions must be positive");
  if (metadata.capture_format.empty()) throw std::invalid_argument("capture_format is required");
  if (metadata.jpeg_quality < 1 || metadata.jpeg_quality > 100) throw std::invalid_argument("jpeg_quality is invalid");
}

void validate(const DetectionResult& result) {
  if (result.schema_version != kSchemaVersion) throw std::invalid_argument("unsupported schema version");
  if (result.source_instance_id.empty() || result.captured_at_unix_ns <= 0) throw std::invalid_argument("detection identity is invalid");
  if (result.image_width == 0 || result.image_height == 0 || result.inference_ms < 0) throw std::invalid_argument("detection dimensions are invalid");
  for (const auto& box : result.boxes) {
    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) || !std::isfinite(box.height) || !std::isfinite(box.confidence)) throw std::invalid_argument("detection contains non-finite values");
    if (box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0 || box.confidence < 0 || box.confidence > 1 || box.x + box.width > result.image_width || box.y + box.height > result.image_height) throw std::invalid_argument("detection box is invalid");
  }
}

}  // namespace face_tracking
