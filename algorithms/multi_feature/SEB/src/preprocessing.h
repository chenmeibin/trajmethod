#pragma once

#include "trajectory_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PreprocessedTrajectoryPart {
  std::string trajectory_id;
  std::vector<RawPoint> points;
};

double raw_point_distance_meters(const RawPoint& lhs, const RawPoint& rhs);

std::vector<RawPoint> remove_isolated_speed_spikes(
    const std::vector<RawPoint>& points, double max_speed_mps,
    std::size_t& removed_count);

double average_sampling_interval_seconds(const std::vector<RawPoint>& points);

std::vector<std::vector<RawPoint>> split_on_time_gap(
    const std::vector<RawPoint>& points, std::int64_t max_gap_seconds);

std::vector<std::vector<RawPoint>> split_by_max_points(
    const std::vector<RawPoint>& points, std::size_t max_points);

std::vector<std::vector<RawPoint>> preprocess_trajectory(
    const std::vector<RawPoint>& points, std::int64_t max_time_gap_seconds,
    double max_speed_mps, std::size_t& removed_spikes);

std::vector<PreprocessedTrajectoryPart> finalize_preprocessed_parts(
    const std::string& source_trajectory_id,
    std::vector<std::vector<RawPoint>> parts,
    double max_average_sampling_interval_seconds,
    std::size_t min_trajectory_points, bool exclude_short_trajectories,
    std::size_t& excluded_sampling_interval, std::size_t& excluded_short);
