#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"
#include "face_tracking/servo/lgpio_pwm.hpp"
#include "face_tracking/servo/servo_driver.hpp"
#include "face_tracking/servo/servo_sweep.hpp"

namespace {
volatile std::sig_atomic_t interrupted = 0;
void stop(int) { interrupted = 1; }

std::int64_t now_unix_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 4) {
      throw std::invalid_argument(
          "usage: face_tracking_servo_sweep_test [config.yaml] [step_deg] [interval_ms]");
    }
    const std::string config_path = argc > 1 ? argv[1] : "config/default.yaml";
    const float step_deg = argc > 2 ? std::stof(argv[2]) : 1.0F;
    const int interval_ms = argc > 3 ? std::stoi(argv[3]) : 50;
    if (interval_ms < 20) throw std::invalid_argument("interval_ms must be at least 20");

    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    const auto settings = face_tracking::load_servo_process_settings(config_path);
    face_tracking::servo::ServoSweep sweep(settings.servo, step_deg);
    face_tracking::servo::LgpioServoPwm pwm;
    face_tracking::servo::ServoDriver driver(settings.servo, pwm);
    driver.start(now_unix_ns());
    std::cout << "Servo sweep started at Home: Pan " << driver.state().commanded_pan_deg
              << " deg, Tilt " << driver.state().commanded_tilt_deg
              << " deg. Press Ctrl+C to stop.\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::uint64_t sequence = 1;
    std::uint64_t log_counter = 0;
    while (!interrupted) {
      const auto previous = sweep.position();
      const auto next = sweep.next();
      const auto now = now_unix_ns();
      face_tracking::PanTiltDelta command{
          .source_instance_id = "servo-sweep-test",
          .tracker_instance_id = "servo-sweep-test",
          .sequence = sequence++,
          .captured_at_unix_ns = now,
          .computed_at_unix_ns = now,
          .selected_track_id = 1,
          .delta_pan_deg = next.pan_deg - previous.pan_deg,
          .delta_tilt_deg = next.tilt_deg - previous.tilt_deg,
          .reason = face_tracking::ControllerDecision::applied,
      };
      const auto& state = driver.process(command, now);
      if (++log_counter % 10 == 0) {
        std::cout << "Pan " << state.commanded_pan_deg << " deg, Tilt "
                  << state.commanded_tilt_deg << " deg\n";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    driver.stop(now_unix_ns());
    std::cout << "Servo sweep stopped; PWM released.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "servo sweep failed: " << error.what() << '\n';
    return 1;
  }
}
