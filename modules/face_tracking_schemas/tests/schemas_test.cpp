#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "face_tracking/schemas/codec.hpp"
#include "face_tracking/schemas/settings.hpp"

TEST(Schemas, FrameMetadataRoundTrips) {
  face_tracking::FrameMetadata value{.source_instance_id = "camera-a", .sequence = 7, .captured_at_unix_ns = 123456, .width = 1280, .height = 720, .capture_format = "MJPG", .jpeg_quality = 80};
  const auto encoded = face_tracking::codec::encode(value);
  const auto decoded = face_tracking::codec::decode_frame_metadata(encoded);
  EXPECT_EQ(decoded.schema_version, 2U);
  EXPECT_EQ(decoded.source_instance_id, "camera-a");
  EXPECT_EQ(decoded.sequence, 7U);
  EXPECT_EQ(decoded.width, 1280U);
}

TEST(Schemas, InvalidMetadataIsRejected) {
  face_tracking::FrameMetadata value;
  EXPECT_THROW(face_tracking::validate(value), std::invalid_argument);
}

TEST(Schemas, PythonGoldenFrameMetadataDecodes) {
  std::ifstream input(std::string(FACE_TRACKING_FIXTURE_DIR) + "/frame_metadata_v2.hex");
  std::string hex;
  input >> hex;
  std::vector<std::uint8_t> bytes;
  for (std::size_t index = 0; index < hex.size(); index += 2) bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(index, 2), nullptr, 16)));
  const auto decoded = face_tracking::codec::decode_frame_metadata(bytes);
  EXPECT_EQ(decoded.source_instance_id, "camera-a");
  EXPECT_EQ(decoded.captured_at_unix_ns, 123456);
}

TEST(Schemas, PythonGoldenDetectionDecodes) {
  std::ifstream input(std::string(FACE_TRACKING_FIXTURE_DIR) + "/detection_result_v2.hex");
  std::string hex;
  input >> hex;
  std::vector<std::uint8_t> bytes;
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(index, 2), nullptr, 16)));
  }
  const auto decoded = face_tracking::codec::decode_detection_result(bytes);
  ASSERT_EQ(decoded.boxes.size(), 1U);
  EXPECT_EQ(decoded.sequence, 7U);
  EXPECT_FLOAT_EQ(decoded.boxes.front().confidence, 0.75F);
}
