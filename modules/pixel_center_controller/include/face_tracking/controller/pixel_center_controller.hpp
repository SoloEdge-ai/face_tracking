#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"

namespace face_tracking::controller {

class PixelCenterController {
 public:
  explicit PixelCenterController(PixelCenterControllerSettings settings);
  ~PixelCenterController();
  PixelCenterController(const PixelCenterController&) = delete;
  PixelCenterController& operator=(const PixelCenterController&) = delete;

  [[nodiscard]] std::optional<PanTiltDelta> process(
      const SelectedTargetObservation& observation, std::int64_t now_unix_ns);
  [[nodiscard]] std::optional<PanTiltDelta> check_timeout(std::int64_t now_unix_ns);
  [[nodiscard]] PixelCenterControllerStatus status() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

class TransportPort {
 public:
  using TargetHandler = std::function<void(SelectedTargetObservation)>;
  virtual ~TransportPort() = default;
  virtual void start(TargetHandler handler) = 0;
  virtual void stop() = 0;
  virtual void publish_delta(const PanTiltDelta& command) = 0;
  virtual void publish_status(const PixelCenterControllerStatus& status) = 0;
};

class PixelCenterControllerService {
 public:
  PixelCenterControllerService(PixelCenterControllerSettings settings, TransportPort& transport);
  ~PixelCenterControllerService();
  PixelCenterControllerService(const PixelCenterControllerService&) = delete;
  PixelCenterControllerService& operator=(const PixelCenterControllerService&) = delete;
  void run(std::stop_token stop_token);

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::controller
