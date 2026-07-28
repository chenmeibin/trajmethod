#include "seb_algorithm.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <queue>
#include <set>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEps = 1e-12;

struct CandidateLowerPriority {
  bool operator()(const SegmentCandidate& lhs,
                  const SegmentCandidate& rhs) const {
    if (lhs.score != rhs.score) {
      return lhs.score < rhs.score;
    }
    if (lhs.point_index != rhs.point_index) {
      return lhs.point_index > rhs.point_index;
    }
    return lhs.start_index > rhs.start_index;
  }
};

struct LocalMotion {
  double dt = 0.0;
  double distance = 0.0;
  double speed = 0.0;
  double direction = 0.0;
};

double transformed_error(double error) {
  return 2.0 / kPi * std::atan(error);
}

SegmentCandidate best_candidate_for_segment(
    const std::vector<MetricPoint>& points,
    const std::vector<LocalMotion>& motion, int start_index, int end_index,
    const ScoreWeights& weights) {
  SegmentCandidate best;
  if (end_index <= start_index + 1) {
    return best;
  }
  const MetricPoint& start = points[start_index];
  const MetricPoint& end = points[end_index];
  const double segment_dt = static_cast<double>(end.time - start.time);
  if (segment_dt <= 0.0) {
    return best;
  }
  const double segment_distance = planar_distance(start, end);
  const double simplified_speed = segment_distance / segment_dt;
  const double simplified_direction = direction_angle(start, end);
  for (int i = start_index + 1; i < end_index; ++i) {
    const MetricPoint& point = points[i];
    const double ratio =
        static_cast<double>(point.time - start.time) / segment_dt;
    if (!(ratio > 0.0 && ratio < 1.0)) {
      continue;
    }
    const double interpolated_x = start.x + ratio * (end.x - start.x);
    const double interpolated_y = start.y + ratio * (end.y - start.y);
    const double sed =
        std::hypot(point.x - interpolated_x, point.y - interpolated_y);
    const double ped = perpendicular_line_distance(point, start, end);
    const double local_dt = motion[i].dt;
    if (local_dt <= 0.0) {
      continue;
    }
    const double sad =
        std::fabs(motion[i].speed - simplified_speed) * local_dt;
    const double delta =
        angle_difference(motion[i].direction, simplified_direction);
    const double dad = motion[i].distance * std::sin(delta);
    const double score = weights.ped * transformed_error(ped) +
                         weights.sed * transformed_error(sed) +
                         weights.sad * transformed_error(sad) +
                         weights.dad * transformed_error(dad);
    if (best.point_index < 0 || score > best.score) {
      best = SegmentCandidate{i, start_index, end_index, score, ped, sed, sad,
                              dad};
    }
  }
  return best;
}

std::size_t target_feature_count(std::size_t point_count, double budget) {
  if (point_count <= 1) {
    return point_count;
  }
  const std::size_t target = static_cast<std::size_t>(
      std::floor(budget * static_cast<double>(point_count)));
  return std::max<std::size_t>(2, std::min(point_count, target));
}

}  // namespace

double planar_distance(const MetricPoint& lhs, const MetricPoint& rhs) {
  return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

double perpendicular_line_distance(const MetricPoint& point,
                                   const MetricPoint& start,
                                   const MetricPoint& end) {
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const double length = std::hypot(dx, dy);
  if (length <= kEps) {
    return planar_distance(point, start);
  }
  const double cross = dx * (point.y - start.y) -
                       dy * (point.x - start.x);
  return std::fabs(cross) / length;
}

double direction_angle(const MetricPoint& start, const MetricPoint& end) {
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  if (std::fabs(dx) <= kEps) {
    if (std::fabs(dy) <= kEps) {
      return 0.0;
    }
    return std::copysign(kPi / 2.0, dy);
  }
  return std::atan(dy / dx);
}

double angle_difference(double lhs, double rhs) {
  const double raw = std::fabs(lhs - rhs);
  return std::min(raw, 2.0 * kPi - raw);
}

SimplificationResult simplify_trajectory(
    const std::vector<MetricPoint>& points, double budget,
    const ScoreWeights& weights, std::int64_t max_interpolation_gap_seconds) {
  SimplificationResult result;
  if (points.size() <= 2) {
    for (std::size_t i = 0; i < points.size(); ++i) {
      result.kept_indices.push_back(static_cast<int>(i));
    }
    return result;
  }
  const std::size_t target = target_feature_count(points.size(), budget);
  std::vector<LocalMotion> motion(points.size() - 1);
  for (std::size_t i = 0; i + 1 < points.size(); ++i) {
    const double dt =
        static_cast<double>(points[i + 1].time - points[i].time);
    const double distance = planar_distance(points[i], points[i + 1]);
    motion[i] = LocalMotion{dt, distance, dt > 0.0 ? distance / dt : 0.0,
                            direction_angle(points[i], points[i + 1])};
  }
  std::set<int> kept = {0, static_cast<int>(points.size() - 1)};
  if (max_interpolation_gap_seconds > 0) {
    for (std::size_t i = 1; i < points.size(); ++i) {
      if (points[i].time - points[i - 1].time >
          max_interpolation_gap_seconds) {
        kept.insert(static_cast<int>(i - 1));
        kept.insert(static_cast<int>(i));
      }
    }
  }
  std::priority_queue<SegmentCandidate, std::vector<SegmentCandidate>,
                      CandidateLowerPriority>
      queue;
  for (auto current = kept.begin(), next = std::next(current);
       next != kept.end(); ++current, ++next) {
    const SegmentCandidate initial =
        best_candidate_for_segment(points, motion, *current, *next, weights);
    if (initial.point_index >= 0) {
      queue.push(initial);
    }
  }
  while (kept.size() < target && !queue.empty()) {
    const SegmentCandidate selected = queue.top();
    queue.pop();
    kept.insert(selected.point_index);
    result.selected_features[selected.point_index] =
        SelectedFeature{result.selected_features.size() + 1, selected};
    const SegmentCandidate left = best_candidate_for_segment(
        points, motion, selected.start_index, selected.point_index, weights);
    const SegmentCandidate right = best_candidate_for_segment(
        points, motion, selected.point_index, selected.end_index, weights);
    if (left.point_index >= 0) {
      queue.push(left);
    }
    if (right.point_index >= 0) {
      queue.push(right);
    }
  }
  result.kept_indices.assign(kept.begin(), kept.end());
  return result;
}
