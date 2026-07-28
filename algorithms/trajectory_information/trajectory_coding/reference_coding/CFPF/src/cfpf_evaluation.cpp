#include "cfpf_evaluation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEarthRadiusMeters = 6378137.0;

struct MetricPoint {
  std::int64_t time = 0;
  double x = 0.0;
  double y = 0.0;
};

double radians(double degrees) { return degrees * kPi / 180.0; }

MetricPoint project(const CfpfFeaturePoint& point, double ref_lon_rad,
                    double ref_lat_rad) {
  return MetricPoint{
      point.time,
      (radians(point.lon) - ref_lon_rad) * std::cos(ref_lat_rad) *
          kEarthRadiusMeters,
      (radians(point.lat) - ref_lat_rad) * kEarthRadiusMeters};
}

double distance(const MetricPoint& lhs, const MetricPoint& rhs) {
  return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

double direction(const MetricPoint& start, const MetricPoint& end) {
  return std::atan2(end.y - start.y, end.x - start.x);
}

double angle_difference(double lhs, double rhs) {
  double difference = std::fabs(lhs - rhs);
  while (difference > 2.0 * kPi) {
    difference -= 2.0 * kPi;
  }
  return difference > kPi ? 2.0 * kPi - difference : difference;
}

double perpendicular_line_distance(const MetricPoint& point,
                                   const MetricPoint& start,
                                   const MetricPoint& end) {
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const double length = std::hypot(dx, dy);
  if (length == 0.0) {
    return distance(point, start);
  }
  return std::fabs(dy * point.x - dx * point.y + end.x * start.y -
                   end.y * start.x) /
         length;
}

}  // namespace

CfpfErrorMetrics evaluate_cfpf_trajectory(
    const CfpfTrajectory& original, const CfpfTrajectory& compressed,
    double reference_lon, double reference_lat) {
  CfpfErrorMetrics metrics;
  metrics.original_points = original.points.size();
  metrics.feature_points = compressed.points.size();
  if (original.points.empty() || compressed.points.size() < 2) {
    return metrics;
  }

  const double ref_lon_rad = radians(reference_lon);
  const double ref_lat_rad = radians(reference_lat);
  std::vector<MetricPoint> original_metric;
  std::vector<MetricPoint> compressed_metric;
  original_metric.reserve(original.points.size());
  compressed_metric.reserve(compressed.points.size());
  std::unordered_map<std::int64_t, std::size_t> original_index_by_time;
  original_index_by_time.reserve(original.points.size());
  for (std::size_t i = 0; i < original.points.size(); ++i) {
    original_metric.push_back(
        project(original.points[i], ref_lon_rad, ref_lat_rad));
    original_index_by_time.emplace(original.points[i].time, i);
  }
  std::vector<std::size_t> feature_indices;
  feature_indices.reserve(compressed.points.size());
  for (const CfpfFeaturePoint& point : compressed.points) {
    const auto found = original_index_by_time.find(point.time);
    if (found == original_index_by_time.end()) {
      throw std::runtime_error("CFPF feature timestamp is absent from original trajectory");
    }
    if (!feature_indices.empty() && found->second <= feature_indices.back()) {
      throw std::runtime_error("CFPF feature timestamps are not strictly ordered");
    }
    feature_indices.push_back(found->second);
    compressed_metric.push_back(project(point, ref_lon_rad, ref_lat_rad));
  }

  long double ped_sum = 0.0;
  long double sed_sum = 0.0;
  long double sad_sum = 0.0;
  long double dad_sum = 0.0;
  long double speed_sum = 0.0;
  long double direction_sum = 0.0;
  for (std::size_t segment = 1; segment < compressed_metric.size(); ++segment) {
    const MetricPoint& start = compressed_metric[segment - 1];
    const MetricPoint& end = compressed_metric[segment];
    const std::size_t start_index = feature_indices[segment - 1];
    const std::size_t end_index = feature_indices[segment];
    const double segment_dt = static_cast<double>(end.time - start.time);
    const double segment_speed =
        segment_dt > 0.0 ? distance(start, end) / segment_dt : 0.0;
    const double segment_direction = direction(start, end);
    for (std::size_t i = start_index; i < end_index; ++i) {
      const MetricPoint& point = original_metric[i];
      const MetricPoint& next = original_metric[i + 1];
      const double ratio =
          segment_dt > 0.0
              ? static_cast<double>(point.time - start.time) / segment_dt
              : 0.0;
      const MetricPoint interpolated{
          point.time, start.x + ratio * (end.x - start.x),
          start.y + ratio * (end.y - start.y)};
      const double ped = perpendicular_line_distance(point, start, end);
      const double sed = distance(point, interpolated);
      const double local_dt = static_cast<double>(next.time - point.time);
      const double local_distance = distance(point, next);
      const double local_speed =
          local_dt > 0.0 ? local_distance / local_dt : 0.0;
      const double speed_error = std::fabs(local_speed - segment_speed);
      const double direction_error =
          angle_difference(direction(point, next), segment_direction);
      const double sad = speed_error * local_dt;
      const double dad = local_distance * std::sin(direction_error);
      ped_sum += ped;
      sed_sum += sed;
      sad_sum += sad;
      dad_sum += dad;
      speed_sum += speed_error;
      direction_sum += direction_error;
      metrics.maximum_ped_m = std::max(metrics.maximum_ped_m, ped);
      metrics.maximum_sed_m = std::max(metrics.maximum_sed_m, sed);
      metrics.maximum_sad_m = std::max(metrics.maximum_sad_m, sad);
      metrics.maximum_dad_m = std::max(metrics.maximum_dad_m, dad);
      metrics.maximum_speed_error_mps =
          std::max(metrics.maximum_speed_error_mps, speed_error);
      metrics.maximum_direction_error_rad =
          std::max(metrics.maximum_direction_error_rad, direction_error);
    }
  }
  const long double denominator =
      static_cast<long double>(original.points.size());
  metrics.average_ped_m = static_cast<double>(ped_sum / denominator);
  metrics.average_sed_m = static_cast<double>(sed_sum / denominator);
  metrics.average_sad_m = static_cast<double>(sad_sum / denominator);
  metrics.average_dad_m = static_cast<double>(dad_sum / denominator);
  metrics.average_speed_error_mps =
      static_cast<double>(speed_sum / denominator);
  metrics.average_direction_error_rad =
      static_cast<double>(direction_sum / denominator);
  return metrics;
}
