#include "face_tracking/camera/camera_service.hpp"
#include "camera_internal.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

namespace face_tracking::camera {
namespace {
std::int64_t unix_now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}

void internal::LatestFrameSlot::update(cv::Mat bgr, std::int64_t captured_at_unix_ns) {
  std::lock_guard lock(mutex_);
  frame_ = CapturedFrame{std::move(bgr), captured_at_unix_ns};
  ++captured_frames_;
}

std::optional<internal::CapturedFrame> internal::LatestFrameSlot::snapshot() const {
  std::lock_guard lock(mutex_);
  if (!frame_) return std::nullopt;
  return internal::CapturedFrame{frame_->bgr.clone(), frame_->captured_at_unix_ns};
}

std::uint64_t internal::LatestFrameSlot::captured_frames() const {
  std::lock_guard lock(mutex_);
  return captured_frames_;
}

internal::PublishGate::PublishGate(OutputPort& output, std::string source_instance_id, const CameraSettings& settings)
    : output_(output), source_instance_id_(std::move(source_instance_id)), settings_(settings) {}

void internal::PublishGate::accept(std::vector<std::uint8_t> jpeg, std::int64_t captured_at_unix_ns) {
  latest_jpeg_ = std::move(jpeg);
  latest_capture_ns_ = captured_at_unix_ns;
}

bool internal::PublishGate::due(std::chrono::steady_clock::time_point now) const { return now >= next_publish_at_; }

bool internal::PublishGate::publish_if_due(std::chrono::steady_clock::time_point now) {
  if (!due(now)) return false;
  next_publish_at_ = now + std::chrono::microseconds(1'000'000 / settings_.publish_hz);
  if (latest_jpeg_.empty() || latest_capture_ns_ == last_published_capture_ns_) return false;
  FrameEvent frame{.jpeg = latest_jpeg_, .metadata = {.source_instance_id = source_instance_id_, .sequence = sequence_, .captured_at_unix_ns = latest_capture_ns_, .width = static_cast<std::uint32_t>(settings_.width), .height = static_cast<std::uint32_t>(settings_.height), .capture_format = "MJPG", .jpeg_quality = static_cast<std::uint32_t>(settings_.jpeg_quality)}};
  output_.publish_frame(frame);
  last_published_capture_ns_ = latest_capture_ns_;
  ++sequence_;
  ++published_frames_;
  return true;
}

std::uint64_t internal::PublishGate::published_frames() const { return published_frames_; }

std::string internal::make_instance_id() {
  std::array<std::uint32_t, 4> values{};
  std::random_device random;
  for (auto& value : values) value = random();
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(8) << values[0] << '-' << std::setw(8) << values[1] << '-' << std::setw(8) << values[2] << '-' << std::setw(8) << values[3];
  return output.str();
}

struct CameraService::Implementation {
  Implementation(CameraSettings value, OutputPort& output)
      : settings(std::move(value)), output(output), gate(output, internal::make_instance_id(), settings) {}

  void capture(std::stop_token stop_token);
  void set_state(CameraState state, std::optional<std::string> error = std::nullopt);
  void publish_status(std::chrono::steady_clock::time_point now, bool force = false);
  void run(std::stop_token stop_token);

  CameraSettings settings;
  OutputPort& output;
  internal::LatestFrameSlot slot;
  internal::PublishGate gate;
  std::mutex state_mutex;
  CameraState state{CameraState::starting};
  std::optional<std::string> last_error;
  std::chrono::steady_clock::time_point last_status_at{};
  std::uint64_t last_capture_count{};
  std::uint64_t last_publish_count{};
};

CameraService::CameraService(CameraSettings settings, OutputPort& output)
    : implementation_(std::make_unique<Implementation>(std::move(settings), output)) {}

CameraService::~CameraService() = default;

void CameraService::Implementation::set_state(CameraState value, std::optional<std::string> error) {
  std::lock_guard lock(state_mutex);
  state = value;
  last_error = std::move(error);
}

void CameraService::Implementation::capture(std::stop_token stop_token) {
  cv::VideoCapture camera;
  while (!stop_token.stop_requested()) {
    if (!camera.isOpened()) {
      camera.open(settings.device_path, cv::CAP_V4L2);
      camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
      camera.set(cv::CAP_PROP_FRAME_WIDTH, settings.width);
      camera.set(cv::CAP_PROP_FRAME_HEIGHT, settings.height);
      camera.set(cv::CAP_PROP_FPS, settings.capture_fps);
      if (!camera.isOpened()) {
        const std::string error = "cannot open camera " + settings.device_path;
        std::cerr << error << '\n';
        set_state(CameraState::reconnecting, error);
        std::this_thread::sleep_for(std::chrono::duration<double>(settings.reconnect_seconds));
        continue;
      }
      set_state(CameraState::streaming);
    }
    cv::Mat frame;
    if (!camera.grab() || !camera.retrieve(frame) || frame.empty()) {
      const std::string error = "camera capture failed";
      set_state(CameraState::error, error);
      camera.release();
      continue;
    }
    slot.update(std::move(frame), unix_now_ns());
  }
  camera.release();
}

void CameraService::Implementation::publish_status(std::chrono::steady_clock::time_point now, bool force) {
  if (!force && last_status_at != std::chrono::steady_clock::time_point{} && now - last_status_at < std::chrono::seconds(1)) return;
  const double elapsed = last_status_at == std::chrono::steady_clock::time_point{} ? 1.0 : std::max(1.0, std::chrono::duration<double>(now - last_status_at).count());
  CameraState current_state;
  std::optional<std::string> error;
  { std::lock_guard lock(state_mutex); current_state = state; error = last_error; }
  const auto captured = slot.captured_frames();
  const auto published = gate.published_frames();
  output.publish_status({.state = current_state, .capture_fps = (captured - last_capture_count) / elapsed, .publish_fps = (published - last_publish_count) / elapsed, .captured_frames = captured, .published_frames = published, .dropped_frames = captured > published ? captured - published : 0, .device_path = settings.device_path, .last_error = error});
  last_status_at = now;
  last_capture_count = captured;
  last_publish_count = published;
}

void CameraService::Implementation::run(std::stop_token stop_token) {
  std::jthread capture_thread([this](std::stop_token token) { capture(token); });
  while (!stop_token.stop_requested()) {
    const auto now = std::chrono::steady_clock::now();
    if (gate.due(now)) {
      if (const auto frame = slot.snapshot()) {
        std::vector<std::uint8_t> jpeg;
        if (cv::imencode(".jpg", frame->bgr, jpeg, {cv::IMWRITE_JPEG_QUALITY, settings.jpeg_quality})) {
          gate.accept(std::move(jpeg), frame->captured_at_unix_ns);
        } else {
          set_state(CameraState::error, "JPEG encoding failed");
        }
      }
      gate.publish_if_due(now);
    }
    publish_status(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  capture_thread.request_stop();
  capture_thread.join();
  set_state(CameraState::stopped);
  publish_status(std::chrono::steady_clock::now(), true);
}

void CameraService::run(std::stop_token stop_token) { implementation_->run(stop_token); }

}  // namespace face_tracking::camera
