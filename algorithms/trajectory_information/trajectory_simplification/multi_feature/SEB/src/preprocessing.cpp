#include "preprocessing.h"

#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEarthRadiusMeters = 6378137.0;

}  // namespace

double raw_point_distance_meters(const RawPoint& lhs, const RawPoint& rhs) {
  const double mean_lat = (lhs.lat + rhs.lat) * 0.5 * kPi / 180.0;
  const double dx = (rhs.lon - lhs.lon) * kPi / 180.0 * std::cos(mean_lat) *
                    kEarthRadiusMeters;
  const double dy = (rhs.lat - lhs.lat) * kPi / 180.0 * kEarthRadiusMeters;
  return std::hypot(dx, dy);
}

std::vector<RawPoint> remove_isolated_speed_spikes(
    const std::vector<RawPoint>& points, double max_speed_mps,
    std::size_t& removed_count) {
  removed_count = 0;
  if (max_speed_mps <= 0.0 || points.size() < 3) {
    return points;
  }
  std::vector<RawPoint> current = points;
  while (current.size() >= 3) {
    std::vector<bool> remove(current.size(), false);
    std::size_t pass_removed = 0;
    for (std::size_t i = 1; i + 1 < current.size(); ++i) {
      const double before_dt =
          static_cast<double>(current[i].time - current[i - 1].time);
      const double after_dt =
          static_cast<double>(current[i + 1].time - current[i].time);
      const double across_dt =
          static_cast<double>(current[i + 1].time - current[i - 1].time);
      if (before_dt <= 0.0 || after_dt <= 0.0 || across_dt <= 0.0) {
        continue;
      }
      const double before_speed =
          raw_point_distance_meters(current[i - 1], current[i]) / before_dt;
      const double after_speed =
          raw_point_distance_meters(current[i], current[i + 1]) / after_dt;
      const double across_speed =
          raw_point_distance_meters(current[i - 1], current[i + 1]) / across_dt;
      if (before_speed > max_speed_mps && after_speed > max_speed_mps &&
          across_speed <= max_speed_mps) {
        remove[i] = true;
        ++pass_removed;
      }
    }
    if (pass_removed == 0) {
      break;
    }
    std::vector<RawPoint> filtered;
    filtered.reserve(current.size() - pass_removed);
    for (std::size_t i = 0; i < current.size(); ++i) {
      if (!remove[i]) {
        filtered.push_back(current[i]);
      }
    }
    removed_count += pass_removed;
    current.swap(filtered);
  }
  return current;
}

double average_sampling_interval_seconds(const std::vector<RawPoint>& points) {
  if (points.size() < 2) {
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(points.back().time - points.front().time) /
         static_cast<double>(points.size() - 1);
}

std::vector<std::vector<RawPoint>> split_on_time_gap(
    const std::vector<RawPoint>& points, std::int64_t max_gap_seconds) {
  if (points.empty()) {
    return {};
  }
  if (max_gap_seconds <= 0) {
    return {points};
  }
  std::vector<std::vector<RawPoint>> parts;
  std::size_t begin = 0;
  for (std::size_t i = 1; i < points.size(); ++i) {
    if (points[i].time - points[i - 1].time > max_gap_seconds) {
      parts.emplace_back(points.begin() + static_cast<std::ptrdiff_t>(begin),
                         points.begin() + static_cast<std::ptrdiff_t>(i));
      begin = i;
    }
  }
  parts.emplace_back(points.begin() + static_cast<std::ptrdiff_t>(begin),
                     points.end());
  return parts;
}

std::vector<std::vector<RawPoint>> split_by_max_points(
    const std::vector<RawPoint>& points, std::size_t max_points) {
  if (points.empty()) {
    return {};
  }
  if (max_points == 0 || points.size() <= max_points) {
    return {points};
  }

  const std::size_t part_count =
      (points.size() + max_points - 1) / max_points;
  const std::size_t base_size = points.size() / part_count;
  const std::size_t larger_parts = points.size() % part_count;
  std::vector<std::vector<RawPoint>> parts;
  parts.reserve(part_count);
  std::size_t begin = 0;
  for (std::size_t i = 0; i < part_count; ++i) {
    const std::size_t part_size = base_size + (i < larger_parts ? 1 : 0);
    const std::size_t end = begin + part_size;
    parts.emplace_back(points.begin() + static_cast<std::ptrdiff_t>(begin),
                       points.begin() + static_cast<std::ptrdiff_t>(end));
    begin = end;
  }
  return parts;
}

std::vector<std::vector<RawPoint>> preprocess_trajectory(
    const std::vector<RawPoint>& points, std::int64_t max_time_gap_seconds,
    double max_speed_mps, std::size_t& removed_spikes) {
  removed_spikes = 0;
  std::vector<std::vector<RawPoint>> result;
  const std::vector<std::vector<RawPoint>> time_parts =
      split_on_time_gap(points, max_time_gap_seconds);
  for (const std::vector<RawPoint>& time_part : time_parts) {
    std::size_t part_removed = 0;
    const std::vector<RawPoint> cleaned =
        remove_isolated_speed_spikes(time_part, max_speed_mps, part_removed);
    removed_spikes += part_removed;
    if (cleaned.empty()) {
      continue;
    }
    std::size_t begin = 0;
    for (std::size_t i = 1; i < cleaned.size(); ++i) {
      const std::int64_t dt = cleaned[i].time - cleaned[i - 1].time;
      const bool time_break =
          max_time_gap_seconds > 0 && dt > max_time_gap_seconds;
      const bool speed_break =
          max_speed_mps > 0.0 && dt > 0 &&
          raw_point_distance_meters(cleaned[i - 1], cleaned[i]) /
                  static_cast<double>(dt) >
              max_speed_mps;
      if (time_break || speed_break) {
        result.emplace_back(cleaned.begin() + static_cast<std::ptrdiff_t>(begin),
                            cleaned.begin() + static_cast<std::ptrdiff_t>(i));
        begin = i;
      }
    }
    result.emplace_back(cleaned.begin() + static_cast<std::ptrdiff_t>(begin),
                        cleaned.end());
  }
  return result;
}

std::vector<PreprocessedTrajectoryPart> finalize_preprocessed_parts(
    const std::string& source_trajectory_id,
    std::vector<std::vector<RawPoint>> parts,
    double max_average_sampling_interval_seconds,
    std::size_t min_trajectory_points, bool exclude_short_trajectories,
    std::size_t& excluded_sampling_interval, std::size_t& excluded_short) {
  std::vector<PreprocessedTrajectoryPart> result;
  result.reserve(parts.size());
  for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
    std::vector<RawPoint>& points = parts[part_index];
    if (max_average_sampling_interval_seconds > 0.0 &&
        average_sampling_interval_seconds(points) >
            max_average_sampling_interval_seconds) {
      ++excluded_sampling_interval;
      continue;
    }
    if (exclude_short_trajectories &&
        points.size() < min_trajectory_points) {
      ++excluded_short;
      continue;
    }
    const std::string trajectory_id =
        parts.size() == 1
            ? source_trajectory_id
            : source_trajectory_id + "_part" +
                  std::to_string(part_index + 1);
    result.push_back(
        PreprocessedTrajectoryPart{trajectory_id, std::move(points)});
  }
  return result;
}
