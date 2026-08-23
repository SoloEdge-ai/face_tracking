#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"

namespace face_tracking::detector {

class FaceTracker {
 public:
  explicit FaceTracker(TrackerSettings settings, std::string instance_id = {});
  ~FaceTracker();
  FaceTracker(const FaceTracker&) = delete;
  FaceTracker& operator=(const FaceTracker&) = delete;

  [[nodiscard]] std::vector<DetectionBox> update(
      const std::vector<DetectionBox>& detections, std::int64_t captured_at_unix_ns);
  void reset();
  [[nodiscard]] const std::string& instance_id() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::detector
