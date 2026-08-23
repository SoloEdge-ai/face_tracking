#include "detector_internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace face_tracking::detector::internal {

struct OpenCvYoloEngine::Implementation {
  explicit Implementation(const DetectorSettings& value) : settings(value), net(cv::dnn::readNetFromONNX(value.model_path)) {
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
  }
  DetectorSettings settings;
  cv::dnn::Net net;
};

OpenCvYoloEngine::OpenCvYoloEngine(const DetectorSettings& settings)
    : implementation_(std::make_unique<Implementation>(settings)) {}

OpenCvYoloEngine::~OpenCvYoloEngine() = default;

std::vector<DetectionBox> OpenCvYoloEngine::infer(const cv::Mat& image) {
  const int input_size = implementation_->settings.image_size;
  const float scale = std::min(static_cast<float>(input_size) / image.cols, static_cast<float>(input_size) / image.rows);
  const int resized_width = static_cast<int>(std::round(image.cols * scale));
  const int resized_height = static_cast<int>(std::round(image.rows * scale));
  const int pad_x = (input_size - resized_width) / 2;
  const int pad_y = (input_size - resized_height) / 2;
  cv::Mat resized;
  cv::resize(image, resized, {resized_width, resized_height});
  cv::Mat letterboxed(input_size, input_size, CV_8UC3, cv::Scalar(114, 114, 114));
  resized.copyTo(letterboxed(cv::Rect(pad_x, pad_y, resized_width, resized_height)));

  auto blob = cv::dnn::blobFromImage(letterboxed, 1.0 / 255.0, {input_size, input_size}, {}, true, false, CV_32F);
  implementation_->net.setInput(blob);
  std::vector<cv::Mat> outputs;
  implementation_->net.forward(outputs, implementation_->net.getUnconnectedOutLayersNames());
  if (outputs.empty() || outputs.front().empty()) throw std::runtime_error("YOLO returned no output");

  const cv::Mat& raw = outputs.front();
  cv::Mat rows;
  if (raw.dims == 3) {
    const int channels = raw.size[1];
    const int predictions = raw.size[2];
    cv::Mat shaped(channels, predictions, CV_32F, const_cast<float*>(raw.ptr<float>()));
    cv::transpose(shaped, rows);
  } else if (raw.dims == 2) {
    rows = raw;
  } else {
    throw std::runtime_error("unsupported YOLO output shape");
  }
  if (rows.cols < 5) throw std::runtime_error("YOLO output has too few channels");

  std::vector<cv::Rect> candidates;
  std::vector<float> scores;
  for (int index = 0; index < rows.rows; ++index) {
    const float* values = rows.ptr<float>(index);
    float confidence = values[4];
    for (int column = 5; column < rows.cols; ++column) confidence = std::max(confidence, values[column]);
    if (confidence < implementation_->settings.confidence) continue;
    const float x1 = (values[0] - values[2] / 2.0F - pad_x) / scale;
    const float y1 = (values[1] - values[3] / 2.0F - pad_y) / scale;
    const float width = values[2] / scale;
    const float height = values[3] / scale;
    const int left = std::clamp(static_cast<int>(std::round(x1)), 0, image.cols - 1);
    const int top = std::clamp(static_cast<int>(std::round(y1)), 0, image.rows - 1);
    const int right = std::clamp(static_cast<int>(std::round(x1 + width)), left + 1, image.cols);
    const int bottom = std::clamp(static_cast<int>(std::round(y1 + height)), top + 1, image.rows);
    candidates.emplace_back(left, top, right - left, bottom - top);
    scores.push_back(confidence);
  }
  std::vector<int> kept;
  cv::dnn::NMSBoxes(candidates, scores, implementation_->settings.confidence, implementation_->settings.iou, kept);
  std::vector<DetectionBox> result;
  result.reserve(kept.size());
  for (const int index : kept) {
    const auto& box = candidates.at(index);
    result.push_back({static_cast<float>(box.x), static_cast<float>(box.y), static_cast<float>(box.width), static_cast<float>(box.height), scores.at(index)});
  }
  return result;
}

}  // namespace face_tracking::detector::internal
