#pragma once

#include <memory>

#include "face_tracking/servo/servo_driver.hpp"

namespace face_tracking::servo {

class LgpioServoPwm final : public ServoPwmPort {
 public:
  LgpioServoPwm();
  ~LgpioServoPwm() override;
  LgpioServoPwm(const LgpioServoPwm&) = delete;
  LgpioServoPwm& operator=(const LgpioServoPwm&) = delete;
  void start(int gpio_chip) override;
  void set_pulse(int gpio, int pulse_us, int frequency_hz) override;
  void stop() noexcept override;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::servo
