#include "face_tracking/bringup/router_manager.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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

void stop_children(const std::vector<pid_t>& children) noexcept {
  for (const auto pid : children) kill(pid, SIGTERM);
  auto remaining = children;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!remaining.empty() && std::chrono::steady_clock::now() < deadline) {
    std::erase_if(remaining, [](pid_t pid) {
      const pid_t result = waitpid(pid, nullptr, WNOHANG);
      return result == pid || (result < 0 && errno == ECHILD);
    });
    if (!remaining.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (const auto pid : remaining) kill(pid, SIGKILL);
  for (const auto pid : remaining) {
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
  }
}
}

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: face_tracking_bringup CAMERA_EXECUTABLE DETECTOR_EXECUTABLE CONTROLLER_EXECUTABLE HMI_PYTHON\n";
    return 2;
  }
  std::vector<pid_t> children;
  try {
    face_tracking::bringup::ensure_system_zenoh();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    children.push_back(spawn({argv[1]}));
    children.push_back(spawn({argv[2]}));
    children.push_back(spawn({argv[3]}));
    children.push_back(spawn({argv[4], "-s", "-m", "face_tracking_hmi"}));
    int child_status = 0;
    while (!interrupted) {
      const pid_t exited = waitpid(-1, &child_status, WNOHANG);
      if (exited > 0) {
        std::erase(children, exited);
        break;
      }
      usleep(100'000);
    }
    stop_children(children);
    children.clear();
    return interrupted ? 0 : 1;
  } catch (const std::exception& error) {
    stop_children(children);
    std::cerr << "bringup failed: " << error.what() << '\n';
    return 1;
  }
}
