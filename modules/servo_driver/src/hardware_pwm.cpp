#include "face_tracking/servo/hardware_pwm.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace face_tracking::servo {
namespace {
namespace fs = std::filesystem;

void write_value(const fs::path& path, const std::string& value) {
  const int descriptor = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (descriptor < 0) {
    throw std::runtime_error("could not open " + path.string() + ": " +
                             std::strerror(errno));
  }
  const ssize_t written = ::write(descriptor, value.data(), value.size());
  const int write_error = errno;
  (void)::close(descriptor);
  if (written != static_cast<ssize_t>(value.size())) {
    throw std::runtime_error("could not write " + path.string() + ": " +
                             std::strerror(write_error));
  }
}

int read_integer(const fs::path& path) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    throw std::runtime_error("could not open " + path.string() + ": " +
                             std::strerror(errno));
  }
  char buffer[32]{};
  const ssize_t count = ::read(descriptor, buffer, sizeof(buffer) - 1);
  const int read_error = errno;
  (void)::close(descriptor);
  if (count <= 0) {
    throw std::runtime_error("could not read " + path.string() + ": " +
                             std::strerror(read_error));
  }
  return std::stoi(std::string(buffer, static_cast<std::size_t>(count)));
}

fs::path wait_for_channel(const fs::path& chip_path, int channel) {
  const fs::path channel_path = chip_path / ("pwm" + std::to_string(channel));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  const auto writable = [&channel_path] {
    return fs::exists(channel_path) &&
           ::access((channel_path / "period").c_str(), W_OK) == 0 &&
           ::access((channel_path / "duty_cycle").c_str(), W_OK) == 0 &&
           ::access((channel_path / "enable").c_str(), W_OK) == 0;
  };
  while (!writable()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("PWM channel did not become writable after export: " +
                               channel_path.string() +
                               "; ensure the current user belongs to the gpio group");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return channel_path;
}
}  // namespace

struct LinuxHardwareServoPwm::Implementation {
  fs::path chip_path;
  int channel_count{0};
  std::set<int> exported_channels;
  std::map<int, int> frequencies;
};

LinuxHardwareServoPwm::LinuxHardwareServoPwm()
    : implementation_(std::make_unique<Implementation>()) {}

LinuxHardwareServoPwm::~LinuxHardwareServoPwm() { stop(); }

int LinuxHardwareServoPwm::channel_for_gpio(int gpio) {
  switch (gpio) {
    case 12:
      return 0;
    case 13:
      return 1;
    case 18:
      return 2;
    case 19:
      return 3;
    default:
      throw std::invalid_argument(
          "GPIO is not a supported Raspberry Pi 5 PWM0 output (12, 13, 18, or 19)");
  }
}

void LinuxHardwareServoPwm::start(int pwm_chip) {
  auto& pwm = *implementation_;
  if (!pwm.chip_path.empty()) throw std::logic_error("hardware PWM is already started");
  if (pwm_chip < 0) throw std::invalid_argument("PWM chip must be non-negative");
  pwm.chip_path = fs::path("/sys/class/pwm") / ("pwmchip" + std::to_string(pwm_chip));
  if (!fs::exists(pwm.chip_path)) {
    const std::string missing_path = pwm.chip_path.string();
    pwm.chip_path.clear();
    throw std::runtime_error(
        "hardware PWM chip is unavailable at " + missing_path +
        "; enable dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2 and reboot");
  }
  pwm.channel_count = read_integer(pwm.chip_path / "npwm");
}

void LinuxHardwareServoPwm::set_pulse(int gpio, int pulse_us, int frequency_hz) {
  auto& pwm = *implementation_;
  if (pwm.chip_path.empty()) throw std::logic_error("hardware PWM is not started");
  if (frequency_hz <= 0 || pulse_us <= 0) {
    throw std::invalid_argument("servo frequency and pulse width must be positive");
  }
  const int channel = channel_for_gpio(gpio);
  if (channel >= pwm.channel_count) {
    throw std::runtime_error("PWM channel " + std::to_string(channel) +
                             " is unavailable on " + pwm.chip_path.string());
  }

  const long long period_ns = 1'000'000'000LL / frequency_hz;
  const long long duty_ns = static_cast<long long>(pulse_us) * 1000LL;
  if (duty_ns >= period_ns) {
    throw std::invalid_argument("servo pulse width must be shorter than the PWM period");
  }

  const auto existing = pwm.frequencies.find(channel);
  if (existing == pwm.frequencies.end()) {
    const fs::path channel_path = pwm.chip_path / ("pwm" + std::to_string(channel));
    if (fs::exists(channel_path)) {
      throw std::runtime_error("PWM channel " + std::to_string(channel) +
                               " is already exported; another process may own it");
    }
    write_value(pwm.chip_path / "export", std::to_string(channel));
    pwm.exported_channels.insert(channel);
    const fs::path exported_path = wait_for_channel(pwm.chip_path, channel);
    write_value(exported_path / "duty_cycle", "0");
    write_value(exported_path / "period", std::to_string(period_ns));
    write_value(exported_path / "duty_cycle", std::to_string(duty_ns));
    write_value(exported_path / "enable", "1");
    pwm.frequencies.emplace(channel, frequency_hz);
    return;
  }

  if (existing->second != frequency_hz) {
    throw std::invalid_argument("changing an active servo PWM frequency is not supported");
  }
  const fs::path channel_path = pwm.chip_path / ("pwm" + std::to_string(channel));
  write_value(channel_path / "duty_cycle", std::to_string(duty_ns));
}

void LinuxHardwareServoPwm::stop() noexcept {
  auto& pwm = *implementation_;
  if (pwm.chip_path.empty()) return;
  for (const int channel : pwm.exported_channels) {
    const fs::path channel_path = pwm.chip_path / ("pwm" + std::to_string(channel));
    try {
      write_value(channel_path / "enable", "0");
    } catch (const std::exception& error) {
      std::cerr << "could not disable PWM channel " << channel << ": " << error.what()
                << '\n';
    }
    try {
      write_value(pwm.chip_path / "unexport", std::to_string(channel));
    } catch (const std::exception& error) {
      std::cerr << "could not release PWM channel " << channel << ": " << error.what()
                << '\n';
    }
  }
  pwm.exported_channels.clear();
  pwm.frequencies.clear();
  pwm.channel_count = 0;
  pwm.chip_path.clear();
}

}  // namespace face_tracking::servo
