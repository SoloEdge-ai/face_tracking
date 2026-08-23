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
  explicit Implementation(const TransportSettings& settings)
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

CameraOutput::CameraOutput(const TransportSettings& settings) : implementation_(std::make_unique<Implementation>(settings)) {}
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
  explicit Implementation(const TransportSettings& settings)
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

  TransportSettings settings;
  zenoh::Session session;
  zenoh::Publisher detection;
  zenoh::Publisher status;
  zenoh::LivelinessToken liveliness;
  std::optional<zenoh::Subscriber<void>> subscriber;
  std::mutex handler_mutex;
  detector::TransportPort::FrameHandler handler;
};

DetectorTransport::DetectorTransport(const TransportSettings& settings) : implementation_(std::make_unique<Implementation>(settings)) {}
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

struct ControllerTransport::Implementation {
  explicit Implementation(const TransportSettings& settings)
      : settings(settings),
        session(open_session(settings.middleware)),
        delta(session.declare_publisher(zenoh::KeyExpr(settings.pan_tilt_delta_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.encoding = zenoh::Encoding::Predefined::application_protobuf();
          return options;
        }())),
        status(session.declare_publisher(zenoh::KeyExpr(settings.controller_status_key()), [] {
          zenoh::Session::PublisherOptions options;
          options.encoding = zenoh::Encoding::Predefined::application_protobuf();
          return options;
        }())),
        liveliness(session.liveliness_declare_token(zenoh::KeyExpr(settings.controller_liveliness_key()))) {}

  TransportSettings settings;
  zenoh::Session session;
  zenoh::Publisher delta;
  zenoh::Publisher status;
  zenoh::LivelinessToken liveliness;
  std::optional<zenoh::Subscriber<void>> subscriber;
  std::mutex handler_mutex;
  controller::TransportPort::TargetHandler handler;
};

ControllerTransport::ControllerTransport(const TransportSettings& settings)
    : implementation_(std::make_unique<Implementation>(settings)) {}
ControllerTransport::~ControllerTransport() = default;

void ControllerTransport::start(TargetHandler handler) {
  {
    std::lock_guard lock(implementation_->handler_mutex);
    implementation_->handler = std::move(handler);
  }
  implementation_->subscriber.emplace(implementation_->session.declare_subscriber(
      zenoh::KeyExpr(implementation_->settings.selected_target_key()),
      [this](const zenoh::Sample& sample) {
        try {
          auto observation = codec::decode_selected_target(sample.get_payload().as_vector());
          std::lock_guard lock(implementation_->handler_mutex);
          if (implementation_->handler) implementation_->handler(std::move(observation));
        } catch (const std::exception&) {
          // Invalid target observations are ignored at the adapter boundary.
        }
      },
      zenoh::closures::none));
}

void ControllerTransport::stop() {
  implementation_->subscriber.reset();
  std::lock_guard lock(implementation_->handler_mutex);
  implementation_->handler = {};
}

void ControllerTransport::publish_delta(const PanTiltDelta& command) {
  implementation_->delta.put(bytes(codec::encode(command)));
}

void ControllerTransport::publish_status(const PixelCenterControllerStatus& status) {
  implementation_->status.put(bytes(codec::encode(status)));
}

struct ServoTransport::Implementation {
  explicit Implementation(const TransportSettings& settings)
      : settings(settings),
        session(open_session(settings.middleware)),
        commanded_state(session.declare_publisher(
            zenoh::KeyExpr(settings.pan_tilt_commanded_state_key()), [] {
              zenoh::Session::PublisherOptions options;
              options.encoding = zenoh::Encoding::Predefined::application_protobuf();
              return options;
            }())),
        liveliness(session.liveliness_declare_token(
            zenoh::KeyExpr(settings.servo_liveliness_key()))) {}

  void upstream(servo::UpstreamEvent event) {
    std::lock_guard lock(handler_mutex);
    if (upstream_handler) upstream_handler(event);
  }

  TransportSettings settings;
  zenoh::Session session;
  zenoh::Publisher commanded_state;
  zenoh::LivelinessToken liveliness;
  std::optional<zenoh::Subscriber<void>> delta_subscriber;
  std::optional<zenoh::Subscriber<void>> controller_liveliness_subscriber;
  std::optional<zenoh::Queryable<void>> state_queryable;
  std::mutex handler_mutex;
  servo::TransportPort::CommandHandler command_handler;
  servo::TransportPort::UpstreamHandler upstream_handler;
  std::mutex state_mutex;
  std::optional<PanTiltCommandedState> latest_state;
};

ServoTransport::ServoTransport(const TransportSettings& settings)
    : implementation_(std::make_unique<Implementation>(settings)) {}
ServoTransport::~ServoTransport() = default;

void ServoTransport::start(CommandHandler command_handler, UpstreamHandler upstream_handler) {
  {
    std::lock_guard lock(implementation_->handler_mutex);
    implementation_->command_handler = std::move(command_handler);
    implementation_->upstream_handler = std::move(upstream_handler);
  }
  implementation_->delta_subscriber.emplace(implementation_->session.declare_subscriber(
      zenoh::KeyExpr(implementation_->settings.pan_tilt_delta_key()),
      [this](const zenoh::Sample& sample) {
        try {
          auto command = codec::decode_pan_tilt_delta(sample.get_payload().as_vector());
          std::lock_guard lock(implementation_->handler_mutex);
          if (implementation_->command_handler) {
            implementation_->command_handler(std::move(command));
          }
        } catch (const std::exception&) {
          // Invalid commands do not refresh upstream activity.
        }
      },
      zenoh::closures::none));
  zenoh::Session::LivelinessSubscriberOptions liveliness_options;
  liveliness_options.history = true;
  implementation_->controller_liveliness_subscriber.emplace(
      implementation_->session.liveliness_declare_subscriber(
          zenoh::KeyExpr(implementation_->settings.controller_liveliness_key()),
          [this](const zenoh::Sample& sample) {
            implementation_->upstream(sample.get_kind() == Z_SAMPLE_KIND_PUT
                                          ? servo::UpstreamEvent::online
                                          : servo::UpstreamEvent::offline);
          },
          zenoh::closures::none, std::move(liveliness_options)));
  zenoh::Session::QueryableOptions queryable_options;
  queryable_options.complete = true;
  implementation_->state_queryable.emplace(implementation_->session.declare_queryable(
      zenoh::KeyExpr(implementation_->settings.pan_tilt_commanded_state_key()),
      [this](const zenoh::Query& query) {
        std::optional<PanTiltCommandedState> state;
        {
          std::lock_guard lock(implementation_->state_mutex);
          state = implementation_->latest_state;
        }
        if (state) {
          query.reply(
              implementation_->settings.pan_tilt_commanded_state_key(),
              bytes(codec::encode(*state)));
        }
      },
      zenoh::closures::none, std::move(queryable_options)));
}

void ServoTransport::stop() {
  implementation_->state_queryable.reset();
  implementation_->controller_liveliness_subscriber.reset();
  implementation_->delta_subscriber.reset();
  std::lock_guard lock(implementation_->handler_mutex);
  implementation_->command_handler = {};
  implementation_->upstream_handler = {};
}

void ServoTransport::publish_state(const PanTiltCommandedState& state) {
  {
    std::lock_guard lock(implementation_->state_mutex);
    implementation_->latest_state = state;
  }
  implementation_->commanded_state.put(bytes(codec::encode(state)));
}

}  // namespace face_tracking::zenoh_adapter
