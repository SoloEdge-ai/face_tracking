#pragma once

#include "face_tracking/schemas/settings.hpp"

namespace face_tracking::servo {

struct SweepPosition {
  float pan_deg{};
  float tilt_deg{};
};

class ServoSweep {
 public:
  ServoSweep(const ServoDriverSettings& settings, float step_deg);

  [[nodiscard]] const SweepPosition& position() const;
 const SweepPosition& next();

 private:
  SweepPosition position_;
  float step_deg_{};
  float pan_direction_{1.0F};
  float tilt_direction_{1.0F};
  float pan_min_{};
  float pan_max_{};
  float tilt_min_{};
  float tilt_max_{};
};

}  // namespace face_tracking::servo
