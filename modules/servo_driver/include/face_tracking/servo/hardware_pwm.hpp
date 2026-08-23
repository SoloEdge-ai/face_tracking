#pragma once

#include <memory>

#include "face_tracking/servo/servo_driver.hpp"

namespace face_tracking::servo {

class LinuxHardwareServoPwm final : public ServoPwmPort {
 public:
  LinuxHardwareServoPwm();
  ~LinuxHardwareServoPwm() override;
  LinuxHardwareServoPwm(const LinuxHardwareServoPwm&) = delete;
  LinuxHardwareServoPwm& operator=(const LinuxHardwareServoPwm&) = delete;

  void start(int pwm_chip) override;
  void set_pulse(int gpio, int pulse_us, int frequency_hz) override;
  void stop() noexcept override;

  [[nodiscard]] static int channel_for_gpio(int gpio);

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::servo
