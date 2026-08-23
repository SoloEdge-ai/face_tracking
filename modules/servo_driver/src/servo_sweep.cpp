#include "face_tracking/servo/servo_sweep.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace face_tracking::servo {
namespace {
void advance(float& position, float& direction, float minimum, float maximum, float step) {
  const float candidate = position + direction * step;
  if (candidate >= maximum) {
    position = maximum;
    direction = -1.0F;
  } else if (candidate <= minimum) {
    position = minimum;
    direction = 1.0F;
  } else {
    position = candidate;
  }
}
}  // namespace

ServoSweep::ServoSweep(const ServoDriverSettings& settings, float step_deg)
    : position_{settings.pan.home_deg, settings.tilt.home_deg},
      step_deg_(step_deg),
      pan_min_(settings.pan.min_deg),
      pan_max_(settings.pan.max_deg),
      tilt_min_(settings.tilt.min_deg),
      tilt_max_(settings.tilt.max_deg) {
  if (!std::isfinite(step_deg) || step_deg <= 0 ||
      step_deg > std::min(settings.max_input_pan_delta_deg,
                          settings.max_input_tilt_delta_deg)) {
    throw std::invalid_argument("servo sweep step exceeds the driver input limit");
  }
}

const SweepPosition& ServoSweep::position() const { return position_; }

const SweepPosition& ServoSweep::next() {
  advance(position_.pan_deg, pan_direction_, pan_min_, pan_max_, step_deg_);
  advance(position_.tilt_deg, tilt_direction_, tilt_min_, tilt_max_, step_deg_);
  return position_;
}

}  // namespace face_tracking::servo
