#pragma once

#include "cfpf_algorithm.h"

#include <cstdint>
#include <vector>

struct CfpfPreprocessingConfig {
  double lcss_point_threshold_m = 75.0;
  double lcss_similarity_threshold = 0.6;
};

struct CfpfPreprocessingStats {
  std::uint64_t spatial_candidate_pairs = 0;
  std::uint64_t exact_lcss_comparisons = 0;
  std::uint64_t lcss_upper_bound_rejections = 0;
};

double lcss_similarity(const CfpfTrajectory& lhs, const CfpfTrajectory& rhs,
                       double point_threshold_m);

std::vector<SimilarTrajectoryGroup> group_similar_trajectories(
    const std::vector<CfpfTrajectory>& trajectories,
    const CfpfPreprocessingConfig& config,
    CfpfPreprocessingStats* stats = nullptr);
