//
//  map_matcher.hpp
//  press-v3
//
//  Created by Renchu Song on 9/27/20.
//  Copyright © 2020 Renc. All rights reserved.
//

#ifndef map_matcher_hpp
#define map_matcher_hpp

#include "../topology/grid_index.hpp"
#include "../topology/sp_table.hpp"
#include "../topology/trajectory.hpp"

struct MapMatchDiagnostics {
  size_t points;
  size_t pointsWithoutCandidates;
  size_t pointsWithZeroMeasurementProbability;
  size_t transitionPairs;
  size_t transitionsWithoutPath;
  size_t transitionsOverDistanceDifference;
  size_t acceptedTransitions;
  size_t chainRestarts;
  size_t outputPoints;
  size_t outputSegments;

  MapMatchDiagnostics();
};

class MapMatcher {
public:
  void mapMatch(
    const SPTable& spTable,
    const Graph& graph,
    const GridIndex& gridIndex,
    double sigmaZ,
    double maxGPSBias,
    double maxDistDifference,
    const GPSTrajectory& gpsTrajectory,
    // GPS trajectory might be broken into multiple sub-trajectories.
    std::vector<GPSTrajectory>& gpsTrajectories,
    std::vector<MapMatchedTrajectory>& mmTrajectories,
    MapMatchDiagnostics* diagnostics = NULL
  ) const;
};

#endif /* map_matcher_hpp */
