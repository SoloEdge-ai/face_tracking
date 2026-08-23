#pragma once

#include <functional>
#include <memory>
#include <stop_token>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"

namespace face_tracking::detector {

class TransportPort {
 public:
  using FrameHandler = std::function<void(FrameEvent)>;
  virtual ~TransportPort() = default;
  virtual void start(FrameHandler handler) = 0;
  virtual void stop() = 0;
  virtual void publish_detection(const DetectionResult& result) = 0;
  virtual void publish_status(const DetectorStatus& status) = 0;
};

class DetectorService {
 public:
  DetectorService(DetectorSettings settings, TransportPort& transport);
  ~DetectorService();
  DetectorService(const DetectorService&) = delete;
  DetectorService& operator=(const DetectorService&) = delete;
  void run(std::stop_token stop_token);

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::detector
