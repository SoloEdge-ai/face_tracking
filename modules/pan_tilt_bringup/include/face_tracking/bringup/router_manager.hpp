#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace face_tracking::bringup {

struct CommandResult {
  int exit_code{};
  std::string output;
};

using Runner = std::function<CommandResult(const std::vector<std::string>&)>;
using Sleeper = std::function<void(std::chrono::seconds)>;

CommandResult run_command(const std::vector<std::string>& command);
void ensure_system_zenoh(const Runner& runner = run_command, std::optional<unsigned int> current_uid = std::nullopt, const Sleeper& sleep = {});

}  // namespace face_tracking::bringup
