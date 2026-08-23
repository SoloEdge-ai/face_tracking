#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"

namespace face_tracking::servo {

class ServoPwmPort {
 public:
  virtual ~ServoPwmPort() = default;
  virtual void start(int gpio_chip) = 0;
  virtual void set_pulse(int gpio, int pulse_us, int frequency_hz) = 0;
  virtual void stop() noexcept = 0;
};

class ServoDriver {
 public:
  ServoDriver(ServoDriverSettings settings, ServoPwmPort& pwm);
  ~ServoDriver();
  ServoDriver(const ServoDriver&) = delete;
  ServoDriver& operator=(const ServoDriver&) = delete;

  const PanTiltCommandedState& start(std::int64_t now_unix_ns);
  const PanTiltCommandedState& process(const PanTiltDelta& command, std::int64_t now_unix_ns);
  void note_upstream_activity(std::int64_t now_unix_ns);
  const PanTiltCommandedState& upstream_lost(std::int64_t now_unix_ns);
  bool check_timeout(std::int64_t now_unix_ns);
  const PanTiltCommandedState& stop(std::int64_t now_unix_ns) noexcept;
  [[nodiscard]] const PanTiltCommandedState& state() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

class TransportPort {
 public:
  using CommandHandler = std::function<void(PanTiltDelta)>;
  using ActivityHandler = std::function<void(bool)>;
  virtual ~TransportPort() = default;
  virtual void start(CommandHandler command_handler, ActivityHandler activity_handler) = 0;
  virtual void stop() = 0;
  virtual void publish_state(const PanTiltCommandedState& state) = 0;
};

class ServoDriverService {
 public:
  ServoDriverService(
      ServoDriverSettings settings, ServoPwmPort& pwm, TransportPort& transport);
  ~ServoDriverService();
  ServoDriverService(const ServoDriverService&) = delete;
  ServoDriverService& operator=(const ServoDriverService&) = delete;
  void run(std::stop_token stop_token);

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::servo
