#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CfpfFeaturePoint {
  std::int64_t time = 0;
  double lon = 0.0;
  double lat = 0.0;
};

struct CfpfTrajectory {
  std::string trajectory_id;
  std::vector<CfpfFeaturePoint> points;
};

struct CfpfConfig {
  double fusion_distance_threshold_m = 50.0;
};

struct SimilarTrajectoryGroup {
  std::vector<std::size_t> trajectory_indices;
};

struct CfpfClusterMember {
  std::size_t trajectory_index = 0;
  std::size_t point_index = 0;
};

struct CfpfFusedCluster {
  double center_lon = 0.0;
  double center_lat = 0.0;
  std::vector<CfpfClusterMember> members;
};

struct CfpfResult {
  std::vector<CfpfFusedCluster> fused_clusters;
};

// Euclidean distance after one fixed local Cartesian projection.  CFPF's
// paper defines Dist(Pi, Pj) in the trajectory coordinate plane; this helper
// keeps the CLI's longitude/latitude storage representation separate from
// that definition.
double planar_distance_m(double lon1, double lat1, double lon2, double lat2,
                         double reference_latitude);

double cfpf_revised_distance_threshold_m(std::size_t trajectory_count,
                                         double distance_threshold_m);

CfpfResult run_cfpf(
    const std::vector<CfpfTrajectory>& trajectories,
    const std::vector<SimilarTrajectoryGroup>& groups,
    const CfpfConfig& config);
