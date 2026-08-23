#include "face_tracking/bringup/router_manager.hpp"

#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }

pid_t spawn(const std::vector<std::string>& command) {
  const pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("could not fork child process");
  if (pid == 0) {
    std::vector<char*> argv;
    for (const auto& value : command) argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  return pid;
}
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: face_tracking_bringup CAMERA_EXECUTABLE DETECTOR_EXECUTABLE HMI_PYTHON\n";
    return 2;
  }
  try {
    face_tracking::bringup::ensure_system_zenoh();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    const std::vector<pid_t> children{
        spawn({argv[1]}),
        spawn({argv[2]}),
        spawn({argv[3], "-s", "-m", "face_tracking_hmi"}),
    };
    int child_status = 0;
    while (!interrupted) {
      const pid_t exited = waitpid(-1, &child_status, WNOHANG);
      if (exited > 0) break;
      usleep(100'000);
    }
    for (const auto pid : children) kill(pid, SIGTERM);
    for (const auto pid : children) waitpid(pid, nullptr, 0);
    return interrupted ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "bringup failed: " << error.what() << '\n';
    return 1;
  }
}
