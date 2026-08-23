#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include "face_tracking/detector/detector_service.hpp"
#include "detector_internal.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: face_tracking_detector_benchmark MODEL IMAGE\n";
    return 2;
  }
  const cv::Mat image = cv::imread(argv[2]);
  if (image.empty()) {
    std::cerr << "could not read benchmark image\n";
    return 2;
  }
  face_tracking::DetectorSettings settings{.model_path = argv[1], .inference_hz = 5, .image_size = 640, .confidence = 0.5F, .iou = 0.45F};
  face_tracking::detector::internal::OpenCvYoloEngine engine(settings);
  for (int index = 0; index < 2; ++index) engine.infer(image);
  std::vector<double> durations;
  std::vector<face_tracking::DetectionBox> boxes;
  for (int index = 0; index < 10; ++index) {
    const auto started = std::chrono::steady_clock::now();
    boxes = engine.infer(image);
    durations.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
  }
  std::sort(durations.begin(), durations.end());
  const double mean = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
  const double p95 = durations[static_cast<std::size_t>(0.95 * (durations.size() - 1))];
  std::cout << "mean_ms=" << mean << " p95_ms=" << p95 << " fps=" << 1000.0 / mean << " boxes=" << boxes.size() << '\n';
  for (const auto& box : boxes) {
    std::cout << "box x=" << box.x << " y=" << box.y << " width=" << box.width
              << " height=" << box.height << " confidence=" << box.confidence << '\n';
  }
  return 0;
}
