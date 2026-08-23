#include "face_tracking/servo/lgpio_pwm.hpp"

#include <lgpio.h>

#include <set>
#include <stdexcept>
#include <string>

namespace face_tracking::servo {
namespace {
void require_success(int result, const char* operation) {
  if (result < 0) {
    throw std::runtime_error(std::string(operation) + ": " + lguErrorText(result));
  }
}
}

struct LgpioServoPwm::Implementation {
  int handle{-1};
  std::set<int> claimed;
};

LgpioServoPwm::LgpioServoPwm() : implementation_(std::make_unique<Implementation>()) {}
LgpioServoPwm::~LgpioServoPwm() { stop(); }

void LgpioServoPwm::start(int gpio_chip) {
  auto& pwm = *implementation_;
  if (pwm.handle >= 0) throw std::logic_error("lgpio PWM is already started");
  pwm.handle = lgGpiochipOpen(gpio_chip);
  require_success(pwm.handle, "could not open gpiochip");
}

void LgpioServoPwm::set_pulse(int gpio, int pulse_us, int frequency_hz) {
  auto& pwm = *implementation_;
  if (pwm.handle < 0) throw std::logic_error("lgpio PWM is not started");
  if (!pwm.claimed.contains(gpio)) {
    require_success(lgGpioClaimOutput(pwm.handle, 0, gpio, 0), "could not claim servo GPIO");
    pwm.claimed.insert(gpio);
  }
  require_success(
      lgTxServo(pwm.handle, gpio, pulse_us, frequency_hz, 0, 0),
      "could not start servo pulses");
}

void LgpioServoPwm::stop() noexcept {
  auto& pwm = *implementation_;
  if (pwm.handle < 0) return;
  for (const int gpio : pwm.claimed) {
    (void)lgTxServo(pwm.handle, gpio, 0, 50, 0, 0);
    (void)lgGpioFree(pwm.handle, gpio);
  }
  pwm.claimed.clear();
  (void)lgGpiochipClose(pwm.handle);
  pwm.handle = -1;
}

}  // namespace face_tracking::servo
