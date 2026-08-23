#include <gtest/gtest.h>

#include <deque>
#include <map>
#include "face_tracking/bringup/router_manager.hpp"

namespace {
class FakeRunner {
 public:
  face_tracking::bringup::CommandResult operator()(const std::vector<std::string>& command) {
    const std::string key = command.empty() ? "" : command[0] + (command.size() > 1 ? " " + command[1] : "");
    calls.push_back(command);
    auto& values = results[key];
    if (values.empty()) return {0, {}};
    auto value = values.front();
    if (values.size() > 1) values.pop_front();
    return value;
  }
  std::map<std::string, std::deque<face_tracking::bringup::CommandResult>> results;
  std::vector<std::vector<std::string>> calls;
};
}

TEST(RouterManager, ReusesActiveService) {
  FakeRunner fake;
  fake.results["systemctl is-active"].push_back({0, {}});
  face_tracking::bringup::ensure_system_zenoh([&](const auto& command) { return fake(command); }, 1000, [](auto) {});
  EXPECT_EQ(fake.calls.size(), 1U);
}

TEST(RouterManager, RefusesUnknownListener) {
  FakeRunner fake;
  fake.results["systemctl is-active"].push_back({1, {}});
  fake.results["sudo ss"].push_back({0, "users:((\"other\",pid=42,fd=3))"});
  fake.results["sudo stat"].push_back({0, "2000\n"});
  fake.results["sudo readlink"].push_back({0, "/usr/bin/other\n"});
  EXPECT_THROW(face_tracking::bringup::ensure_system_zenoh([&](const auto& command) { return fake(command); }, 1000, [](auto) {}), std::runtime_error);
}
