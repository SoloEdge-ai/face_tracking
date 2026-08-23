#include "face_tracking/bringup/router_manager.hpp"

#include <array>
#include <cerrno>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace face_tracking::bringup {
namespace {
bool active(const Runner& run) { return run({"systemctl", "is-active", "--quiet", "zenohd"}).exit_code == 0; }

std::vector<int> listeners(const Runner& run) {
  const auto result = run({"sudo", "ss", "-ltnp", "sport", "=", ":7447"});
  const std::regex pattern("pid=(\\d+)");
  std::set<int> unique;
  for (auto it = std::sregex_iterator(result.output.begin(), result.output.end(), pattern); it != std::sregex_iterator(); ++it) unique.insert(std::stoi((*it)[1]));
  return {unique.begin(), unique.end()};
}

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
  return value;
}
}

CommandResult run_command(const std::vector<std::string>& command) {
  if (command.empty()) return {1, "empty command"};
  int pipe_fd[2];
  if (pipe(pipe_fd) != 0) throw std::runtime_error("pipe failed");
  const pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[1]);
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const auto& item : command) argv.push_back(const_cast<char*>(item.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(pipe_fd[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  ssize_t read_count;
  while ((read_count = read(pipe_fd[0], buffer.data(), buffer.size())) > 0) output.append(buffer.data(), static_cast<std::size_t>(read_count));
  close(pipe_fd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return {WIFEXITED(status) ? WEXITSTATUS(status) : 128, std::move(output)};
}

void ensure_system_zenoh(const Runner& run, std::optional<unsigned int> current_uid, const Sleeper& custom_sleep) {
  if (active(run)) return;
  const auto sleep = custom_sleep ? custom_sleep : Sleeper([](std::chrono::seconds duration) { std::this_thread::sleep_for(duration); });
  const unsigned int uid = current_uid.value_or(getuid());
  for (const int pid : listeners(run)) {
    const auto uid_result = run({"sudo", "stat", "-c", "%u", "/proc/" + std::to_string(pid)});
    const auto exe_result = run({"sudo", "readlink", "-f", "/proc/" + std::to_string(pid) + "/exe"});
    if (uid_result.exit_code != 0 || exe_result.exit_code != 0 || trim(uid_result.output) != std::to_string(uid) || trim(exe_result.output) != "/usr/bin/zenohd") {
      throw std::runtime_error("TCP 7447 is owned by a process that cannot be safely replaced: pid=" + std::to_string(pid));
    }
    if (run({"kill", "-INT", std::to_string(pid)}).exit_code != 0) throw std::runtime_error("could not interrupt manual zenohd");
  }
  for (int attempt = 0; attempt < 10 && !listeners(run).empty(); ++attempt) sleep(std::chrono::seconds(1));
  if (!listeners(run).empty()) throw std::runtime_error("manual zenohd did not release TCP 7447 within 10 seconds");
  run({"sudo", "systemctl", "reset-failed", "zenohd"});
  const auto started = run({"sudo", "systemctl", "start", "zenohd"});
  if (started.exit_code != 0) throw std::runtime_error("zenohd.service failed to start: " + started.output);
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (active(run) && !listeners(run).empty()) return;
    sleep(std::chrono::seconds(1));
  }
  throw std::runtime_error("zenohd.service is not active and listening on TCP 7447");
}

}  // namespace face_tracking::bringup
