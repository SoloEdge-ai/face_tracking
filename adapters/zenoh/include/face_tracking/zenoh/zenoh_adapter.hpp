#pragma once

#include <memory>

#include "face_tracking/camera/camera_service.hpp"
#include "face_tracking/detector/detector_service.hpp"
#include "face_tracking/schemas/settings.hpp"

namespace face_tracking::zenoh_adapter {

class CameraOutput final : public camera::OutputPort {
 public:
  explicit CameraOutput(const TransportSettings& settings);
  ~CameraOutput() override;
  CameraOutput(const CameraOutput&) = delete;
  CameraOutput& operator=(const CameraOutput&) = delete;
  void publish_frame(const FrameEvent& frame) override;
  void publish_status(const CameraStatus& status) override;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

class DetectorTransport final : public detector::TransportPort {
 public:
  explicit DetectorTransport(const TransportSettings& settings);
  ~DetectorTransport() override;
  DetectorTransport(const DetectorTransport&) = delete;
  DetectorTransport& operator=(const DetectorTransport&) = delete;
  void start(FrameHandler handler) override;
  void stop() override;
  void publish_detection(const DetectionResult& result) override;
  void publish_status(const DetectorStatus& status) override;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace face_tracking::zenoh_adapter
