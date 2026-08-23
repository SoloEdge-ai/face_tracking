#include "face_tracking/zenoh/zenoh_adapter.hpp"

#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "face_tracking/schemas/codec.hpp"
#include "zenoh.hxx"

namespace face_tracking::zenoh_adapter {
namespace {
zenoh::Session open_session(const MiddlewareSettings& settings) {
  auto config = zenoh::Config::create_default();
  config.insert_json5("mode", "\"client\"");
  config.insert_json5("connect/endpoints", "[\"" + settings.connect + "\"]");
  return zenoh::Session::open(std::move(config));
}

zenoh::Bytes bytes(std::vector<std::uint8_t> value) { return zenoh::Bytes(std::move(value)); }
}

struct CameraOutput::Implementation {
  explicit Implementation(const Settings& settings)
      : session(open_session(settings.middleware)),
        image(session.declare_publisher(zenoh::KeyExpr(settings.camera_image_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.congestion_control = Z_CONGESTION_CONTROL_DROP;
          options.priority = Z_PRIORITY_DATA_LOW;
          options.encoding = zenoh::Encoding::Predefined::image_jpeg();
          return options;
        }())),
        status(session.declare_publisher(zenoh::KeyExpr(settings.camera_status_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.encoding = zenoh::Encoding::Predefined::application_protobuf();
          return options;
        }())),
        liveliness(session.liveliness_declare_token(zenoh::KeyExpr(settings.camera_liveliness_key()))) {}

  zenoh::Session session;
  zenoh::Publisher image;
  zenoh::Publisher status;
  zenoh::LivelinessToken liveliness;
};

CameraOutput::CameraOutput(const Settings& settings) : implementation_(std::make_unique<Implementation>(settings)) {}
CameraOutput::~CameraOutput() = default;

void CameraOutput::publish_frame(const FrameEvent& frame) {
  zenoh::Publisher::PutOptions options;
  options.attachment = bytes(codec::encode(frame.metadata));
  implementation_->image.put(bytes(frame.jpeg), std::move(options));
}

void CameraOutput::publish_status(const CameraStatus& status) {
  implementation_->status.put(bytes(codec::encode(status)));
}

struct DetectorTransport::Implementation {
  explicit Implementation(const Settings& settings)
      : settings(settings),
        session(open_session(settings.middleware)),
        detection(session.declare_publisher(zenoh::KeyExpr(settings.detections_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.encoding = zenoh::Encoding::Predefined::application_protobuf();
          return options;
        }())),
        status(session.declare_publisher(zenoh::KeyExpr(settings.detector_status_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.encoding = zenoh::Encoding::Predefined::application_protobuf();
          return options;
        }())),
        liveliness(session.liveliness_declare_token(zenoh::KeyExpr(settings.detector_liveliness_key()))) {}

  Settings settings;
  zenoh::Session session;
  zenoh::Publisher detection;
  zenoh::Publisher status;
  zenoh::LivelinessToken liveliness;
  std::optional<zenoh::Subscriber<void>> subscriber;
  std::mutex handler_mutex;
  detector::TransportPort::FrameHandler handler;
};

DetectorTransport::DetectorTransport(const Settings& settings) : implementation_(std::make_unique<Implementation>(settings)) {}
DetectorTransport::~DetectorTransport() = default;

void DetectorTransport::start(FrameHandler handler) {
  {
    std::lock_guard lock(implementation_->handler_mutex);
    implementation_->handler = std::move(handler);
  }
  implementation_->subscriber.emplace(implementation_->session.declare_subscriber(
      zenoh::KeyExpr(implementation_->settings.camera_image_key()),
      [this](const zenoh::Sample& sample) {
        try {
          const auto attachment = sample.get_attachment();
          if (!attachment) throw std::invalid_argument("camera frame is missing metadata");
          FrameEvent frame{.jpeg = sample.get_payload().as_vector(), .metadata = codec::decode_frame_metadata(attachment->get().as_vector())};
          std::lock_guard lock(implementation_->handler_mutex);
          if (implementation_->handler) implementation_->handler(std::move(frame));
        } catch (const std::exception&) {
          // Invalid transport samples are ignored; the service reports decode errors for valid envelopes.
        }
      },
      zenoh::closures::none));
}

void DetectorTransport::stop() {
  implementation_->subscriber.reset();
  std::lock_guard lock(implementation_->handler_mutex);
  implementation_->handler = {};
}

void DetectorTransport::publish_detection(const DetectionResult& result) {
  implementation_->detection.put(bytes(codec::encode(result)));
}

void DetectorTransport::publish_status(const DetectorStatus& status) {
  implementation_->status.put(bytes(codec::encode(status)));
}

}  // namespace face_tracking::zenoh_adapter
