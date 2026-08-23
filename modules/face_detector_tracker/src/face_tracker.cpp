#include "face_tracking/detector/face_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace face_tracking::detector {
namespace {
constexpr double kUnmatchedCost = 1.1;
constexpr double kInvalidCost = 1000.0;

std::string make_instance_id() {
  std::random_device random;
  std::ostringstream value;
  value << std::hex << std::setfill('0');
  for (int index = 0; index < 4; ++index) value << std::setw(8) << random();
  return value.str();
}

float intersection_over_union(const DetectionBox& left, const DetectionBox& right) {
  const float x1 = std::max(left.x, right.x);
  const float y1 = std::max(left.y, right.y);
  const float x2 = std::min(left.x + left.width, right.x + right.width);
  const float y2 = std::min(left.y + left.height, right.y + right.height);
  const float intersection = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
  const float combined = left.width * left.height + right.width * right.height - intersection;
  return combined > 0 ? intersection / combined : 0;
}

std::vector<int> minimum_cost_assignment(const std::vector<std::vector<double>>& cost) {
  const std::size_t size = cost.size();
  std::vector<double> row_potential(size + 1), column_potential(size + 1);
  std::vector<int> matched_row(size + 1), previous_column(size + 1);
  for (std::size_t row = 1; row <= size; ++row) {
    matched_row[0] = static_cast<int>(row);
    std::size_t column = 0;
    std::vector<double> minimum(size + 1, std::numeric_limits<double>::infinity());
    std::vector<bool> used(size + 1);
    do {
      used[column] = true;
      const int active_row = matched_row[column];
      double delta = std::numeric_limits<double>::infinity();
      std::size_t next_column = 0;
      for (std::size_t candidate = 1; candidate <= size; ++candidate) {
        if (used[candidate]) continue;
        const double current = cost[active_row - 1][candidate - 1] - row_potential[active_row] - column_potential[candidate];
        if (current < minimum[candidate]) {
          minimum[candidate] = current;
          previous_column[candidate] = static_cast<int>(column);
        }
        if (minimum[candidate] < delta) {
          delta = minimum[candidate];
          next_column = candidate;
        }
      }
      for (std::size_t candidate = 0; candidate <= size; ++candidate) {
        if (used[candidate]) {
          row_potential[matched_row[candidate]] += delta;
          column_potential[candidate] -= delta;
        } else {
          minimum[candidate] -= delta;
        }
      }
      column = next_column;
    } while (matched_row[column] != 0);
    do {
      const auto previous = static_cast<std::size_t>(previous_column[column]);
      matched_row[column] = matched_row[previous];
      column = previous;
    } while (column != 0);
  }
  std::vector<int> assignment(size, -1);
  for (std::size_t column = 1; column <= size; ++column) {
    if (matched_row[column] > 0) assignment[matched_row[column] - 1] = static_cast<int>(column - 1);
  }
  return assignment;
}
}  // namespace

struct FaceTracker::Implementation {
  struct Track {
    std::uint64_t id{};
    DetectionBox box;
    float velocity_x{};
    float velocity_y{};
    std::int64_t last_seen_unix_ns{};
  };

  TrackerSettings settings;
  std::string instance_id;
  std::vector<Track> tracks;
  std::uint64_t next_id{1};
  std::int64_t last_update_unix_ns{};

  DetectionBox predicted(const Track& track, std::int64_t now) const {
    auto box = track.box;
    const double seconds = std::max(0.0, (now - track.last_seen_unix_ns) / 1'000'000'000.0);
    box.x += static_cast<float>(track.velocity_x * seconds);
    box.y += static_cast<float>(track.velocity_y * seconds);
    return box;
  }

  double association_cost(const Track& track, const DetectionBox& detection, std::int64_t now) const {
    const auto expected = predicted(track, now);
    const float iou = intersection_over_union(expected, detection);
    const float dx = (expected.x + expected.width / 2) - (detection.x + detection.width / 2);
    const float dy = (expected.y + expected.height / 2) - (detection.y + detection.height / 2);
    const float scale = std::max(1.0F, std::max(std::hypot(expected.width, expected.height), std::hypot(detection.width, detection.height)));
    const float distance_ratio = std::hypot(dx, dy) / scale;
    if (iou < settings.min_match_iou && distance_ratio > settings.max_center_distance_ratio) return kInvalidCost;
    return 0.7 * (1.0 - iou) + 0.3 * std::min(1.0F, distance_ratio);
  }
};

FaceTracker::FaceTracker(TrackerSettings settings, std::string instance_id)
      : implementation_(std::make_unique<Implementation>(Implementation{
          .settings = settings,
          .instance_id = instance_id.empty() ? make_instance_id() : std::move(instance_id),
          .tracks = {},
      })) {}

FaceTracker::~FaceTracker() = default;

std::vector<DetectionBox> FaceTracker::update(
    const std::vector<DetectionBox>& detections, std::int64_t captured_at_unix_ns) {
  auto& state = *implementation_;
  if (captured_at_unix_ns <= 0) return {};
  if (state.last_update_unix_ns > captured_at_unix_ns) state.tracks.clear();
  state.last_update_unix_ns = captured_at_unix_ns;
  const auto retention_ns = static_cast<std::int64_t>(state.settings.retention_ms) * 1'000'000;
  std::erase_if(state.tracks, [&](const auto& track) {
    return captured_at_unix_ns - track.last_seen_unix_ns > retention_ns;
  });

  std::vector<DetectionBox> output = detections;
  const std::size_t size = std::max(state.tracks.size(), detections.size());
  std::vector<bool> detection_used(detections.size());
  if (size > 0) {
    std::vector<std::vector<double>> costs(size, std::vector<double>(size, kUnmatchedCost));
    for (std::size_t track = 0; track < state.tracks.size(); ++track) {
      for (std::size_t detection = 0; detection < detections.size(); ++detection) {
        costs[track][detection] = state.association_cost(state.tracks[track], detections[detection], captured_at_unix_ns);
      }
    }
    const auto assignment = minimum_cost_assignment(costs);
    for (std::size_t track_index = 0; track_index < state.tracks.size(); ++track_index) {
      const int detection_index = assignment[track_index];
      if (detection_index < 0 || static_cast<std::size_t>(detection_index) >= detections.size() ||
          costs[track_index][detection_index] >= kUnmatchedCost) continue;
      auto& track = state.tracks[track_index];
      const auto& detection = detections[detection_index];
      const double seconds = (captured_at_unix_ns - track.last_seen_unix_ns) / 1'000'000'000.0;
      if (seconds > 0) {
        const float old_center_x = track.box.x + track.box.width / 2;
        const float old_center_y = track.box.y + track.box.height / 2;
        const float new_center_x = detection.x + detection.width / 2;
        const float new_center_y = detection.y + detection.height / 2;
        track.velocity_x = 0.5F * track.velocity_x + 0.5F * static_cast<float>((new_center_x - old_center_x) / seconds);
        track.velocity_y = 0.5F * track.velocity_y + 0.5F * static_cast<float>((new_center_y - old_center_y) / seconds);
      }
      track.box = detection;
      track.box.track_id = track.id;
      track.last_seen_unix_ns = captured_at_unix_ns;
      output[detection_index].track_id = track.id;
      detection_used[detection_index] = true;
    }
  }

  for (std::size_t index = 0; index < detections.size(); ++index) {
    if (detection_used[index]) continue;
    const auto id = state.next_id++;
    output[index].track_id = id;
    auto box = output[index];
    state.tracks.push_back({.id = id, .box = box, .last_seen_unix_ns = captured_at_unix_ns});
  }
  return output;
}

const std::string& FaceTracker::instance_id() const { return implementation_->instance_id; }

}  // namespace face_tracking::detector
