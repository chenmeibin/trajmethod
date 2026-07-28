#pragma once

#include "trajectory_types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

struct ScoreWeights {
  double ped = 0.3;
  double sed = 0.5;
  double sad = 0.1;
  double dad = 0.1;
};

struct SegmentCandidate {
  int point_index = -1;
  int start_index = -1;
  int end_index = -1;
  double score = -1.0;
  double ped = 0.0;
  double sed = 0.0;
  double sad = 0.0;
  double dad = 0.0;
};

struct SelectedFeature {
  std::size_t rank = 0;
  SegmentCandidate candidate;
};

struct SimplificationResult {
  std::vector<int> kept_indices;
  std::map<int, SelectedFeature> selected_features;
};

double planar_distance(const MetricPoint& lhs, const MetricPoint& rhs);
double perpendicular_line_distance(const MetricPoint& point,
                                   const MetricPoint& start,
                                   const MetricPoint& end);
double direction_angle(const MetricPoint& start, const MetricPoint& end);
double angle_difference(double lhs, double rhs);

SimplificationResult simplify_trajectory(
    const std::vector<MetricPoint>& points, double budget,
    const ScoreWeights& weights, std::int64_t max_interpolation_gap_seconds);
