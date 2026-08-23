#pragma once

#include <memory>
#include <stop_token>

#include "face_tracking/schemas/settings.hpp"
#include "face_tracking/schemas/types.hpp"

namespace face_tracking::camera {

class OutputPort {
 public:
  virtual ~OutputPort() = default;
  virtual void publish_frame(const FrameEvent& frame) = 0;
  virtual void publish_status(const CameraStatus& status) = 0;
};

class CameraService {
 public:
  CameraService(CameraSettings settings, OutputPort& output);
  ~CameraService();
  CameraService(const CameraService&) = delete;
  CameraService& operator=(const CameraService&) = delete;
  void run(std::stop_token stop_token);

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::camera
