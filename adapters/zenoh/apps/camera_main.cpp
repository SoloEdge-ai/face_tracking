#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <thread>

#include "face_tracking/camera/camera_service.hpp"
#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/zenoh/zenoh_adapter.hpp"

namespace {
volatile std::sig_atomic_t interrupted = 0;
void stop(int) { interrupted = 1; }
}

int main() {
  try {
    std::stop_source source;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    const char* configured = std::getenv("FACE_TRACKING_CONFIG");
    const auto settings = face_tracking::load_camera_process_settings(configured ? configured : "config/default.yaml");
    if (settings.transport.middleware.adapter != "zenoh") throw std::runtime_error("camera Zenoh executable requires middleware.adapter=zenoh");
    face_tracking::zenoh_adapter::CameraOutput output(settings.transport);
    face_tracking::camera::CameraService service(settings.camera, output);
    std::jthread signal_monitor([&source](std::stop_token token) {
      while (!token.stop_requested() && !interrupted) std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (interrupted) source.request_stop();
    });
    service.run(source.get_token());
    signal_monitor.request_stop();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "camera service failed: " << error.what() << '\n';
    return 1;
  }
}
