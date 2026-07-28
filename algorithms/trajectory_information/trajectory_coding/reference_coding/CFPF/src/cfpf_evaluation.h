#pragma once

#include "cfpf_algorithm.h"

#include <cstddef>

struct CfpfErrorMetrics {
  std::size_t original_points = 0;
  std::size_t feature_points = 0;
  double average_ped_m = 0.0;
  double maximum_ped_m = 0.0;
  double average_sed_m = 0.0;
  double maximum_sed_m = 0.0;
  double average_sad_m = 0.0;
  double maximum_sad_m = 0.0;
  double average_dad_m = 0.0;
  double maximum_dad_m = 0.0;
  double average_speed_error_mps = 0.0;
  double maximum_speed_error_mps = 0.0;
  double average_direction_error_rad = 0.0;
  double maximum_direction_error_rad = 0.0;
};

CfpfErrorMetrics evaluate_cfpf_trajectory(
    const CfpfTrajectory& original, const CfpfTrajectory& compressed,
    double reference_lon, double reference_lat);
