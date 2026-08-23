#include "face_tracking/detector/detector_service.hpp"
#include "detector_internal.hpp"

#include <algorithm>
#include <iostream>
#include <thread>

#include <opencv2/imgcodecs.hpp>

namespace face_tracking::detector {

bool internal::LatestFrameSlot::update(FrameEvent frame) {
  const auto key = std::pair{frame.metadata.source_instance_id, frame.metadata.sequence};
  std::lock_guard lock(mutex_);
  if (last_key_ == key) return false;
  if (frame_) ++dropped_frames_;
  frame_ = std::move(frame);
  last_key_ = key;
  return true;
}

std::optional<FrameEvent> internal::LatestFrameSlot::take() {
  std::lock_guard lock(mutex_);
  auto frame = std::move(frame_);
  frame_.reset();
  return frame;
}

std::uint64_t internal::LatestFrameSlot::dropped_frames() const {
  std::lock_guard lock(mutex_);
  return dropped_frames_;
}

internal::DetectorLoop::DetectorLoop(TransportPort& transport, InferenceEngine& engine, int inference_hz)
    : transport_(transport), engine_(engine), inference_hz_(inference_hz) {}

bool internal::DetectorLoop::due(std::chrono::steady_clock::time_point now) const { return now >= next_inference_at_; }

bool internal::DetectorLoop::process_if_due(const std::optional<FrameEvent>& frame, std::chrono::steady_clock::time_point now) {
  if (!due(now)) return false;
  next_inference_at_ = now + std::chrono::microseconds(1'000'000 / inference_hz_);
  if (!frame) return false;
  const auto key = std::pair{frame->metadata.source_instance_id, frame->metadata.sequence};
  if (last_processed_key_ == key) return false;
  const cv::Mat image = cv::imdecode(frame->jpeg, cv::IMREAD_COLOR);
  if (image.empty()) {
    ++decode_errors_;
    last_processed_key_ = key;
    return false;
  }
  const auto started = std::chrono::steady_clock::now();
  auto boxes = engine_.infer(image);
  const auto inference_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  DetectionResult result{.source_instance_id = frame->metadata.source_instance_id, .sequence = frame->metadata.sequence, .captured_at_unix_ns = frame->metadata.captured_at_unix_ns, .image_width = frame->metadata.width, .image_height = frame->metadata.height, .inference_ms = inference_ms, .boxes = std::move(boxes)};
  validate(result);
  transport_.publish_detection(result);
  last_processed_key_ = key;
  ++processed_frames_;
  return true;
}

std::uint64_t internal::DetectorLoop::processed_frames() const { return processed_frames_; }
std::uint64_t internal::DetectorLoop::decode_errors() const { return decode_errors_; }

struct DetectorService::Implementation {
  Implementation(DetectorSettings value, TransportPort& transport)
      : settings(std::move(value)),
        transport(transport),
        engine(std::make_unique<internal::OpenCvYoloEngine>(settings)),
        loop(transport, *engine, settings.inference_hz) {}

  void publish_status(std::chrono::steady_clock::time_point now, bool force = false);
  void run(std::stop_token stop_token);

  DetectorSettings settings;
  TransportPort& transport;
  std::unique_ptr<internal::InferenceEngine> engine;
  internal::LatestFrameSlot slot;
  internal::DetectorLoop loop;
  DetectorState state{DetectorState::starting};
  std::optional<std::string> last_error;
  std::uint64_t inference_errors{};
  std::chrono::steady_clock::time_point last_status_at{};
  std::uint64_t last_processed_count{};
};

DetectorService::DetectorService(DetectorSettings settings, TransportPort& transport)
    : implementation_(std::make_unique<Implementation>(std::move(settings), transport)) {}

DetectorService::~DetectorService() = default;

void DetectorService::Implementation::publish_status(std::chrono::steady_clock::time_point now, bool force) {
  if (!force && last_status_at != std::chrono::steady_clock::time_point{} && now - last_status_at < std::chrono::seconds(1)) return;
  const double elapsed = last_status_at == std::chrono::steady_clock::time_point{} ? 1.0 : std::max(1.0, std::chrono::duration<double>(now - last_status_at).count());
  const auto processed = loop.processed_frames();
  transport.publish_status({.state = state, .inference_fps = (processed - last_processed_count) / elapsed, .processed_frames = processed, .dropped_frames = slot.dropped_frames(), .decode_errors = loop.decode_errors(), .inference_errors = inference_errors, .last_error = last_error});
  last_status_at = now;
  last_processed_count = processed;
}

void DetectorService::Implementation::run(std::stop_token stop_token) {
  transport.start([this](FrameEvent frame) { slot.update(std::move(frame)); });
  state = DetectorState::streaming;
  while (!stop_token.stop_requested()) {
    const auto now = std::chrono::steady_clock::now();
    try {
      const bool processed = loop.due(now) && loop.process_if_due(slot.take(), now);
      if (processed && state == DetectorState::error) {
        state = DetectorState::streaming;
        last_error.reset();
      }
    } catch (const std::exception& error) {
      state = DetectorState::error;
      last_error = error.what();
      ++inference_errors;
      std::cerr << "face inference failed: " << error.what() << '\n';
    }
    publish_status(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  transport.stop();
  state = DetectorState::stopped;
  publish_status(std::chrono::steady_clock::now(), true);
}

void DetectorService::run(std::stop_token stop_token) { implementation_->run(stop_token); }

}  // namespace face_tracking::detector
