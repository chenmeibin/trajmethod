#ifndef TRACE_ONLINE_MAP_MATCHER_HPP
#define TRACE_ONLINE_MAP_MATCHER_HPP

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "../vendor_press/topology/grid_index.hpp"
#include "../vendor_press/topology/trajectory.hpp"

struct OnlineMapMatchingParameters {
  double initialSearchRadius;
  double predictionThreshold;
  double updateThreshold;
  double accelerationSigma;
  double observationSigma;
  double velocityObservationSigma;

  OnlineMapMatchingParameters();
};

struct OnlineMatchedPoint {
  int edgeId;
  double networkDistanceOnEdge;
  double timestamp;
  double posteriorProbability;

  OnlineMatchedPoint();
};

struct OnlineMapMatchingStats {
  size_t pointsProcessed;
  size_t pointsMatched;
  size_t initializations;
  size_t predictionCandidates;
  size_t prunedCandidates;
  size_t predictionBranches;

  OnlineMapMatchingStats();
};

// Approximate implementation of Taguchi et al.'s Algorithms 1 and 2.
// Each push consumes only the current and past GPS observations.
class OnlineMapMatcher {
public:
  OnlineMapMatcher(
    const Graph& graph,
    const GridIndex& gridIndex,
    const OnlineMapMatchingParameters& parameters =
      OnlineMapMatchingParameters()
  );

  bool push(const GPSPoint& gpsPoint, OnlineMatchedPoint& matchedPoint);
  void reset();
  const OnlineMapMatchingStats& getStats() const;
  size_t getCandidateCount() const;

private:
  struct Candidate {
    int edgeId;
    double weight;

    Candidate(int edge, double probability)
      : edgeId(edge), weight(probability) { }
  };

  const Graph& graph_;
  const GridIndex& gridIndex_;
  OnlineMapMatchingParameters parameters_;
  std::vector<Candidate> candidates_;
  bool hasPreviousGps_;
  bool hasVelocityEstimate_;
  GPSPoint previousGps_;
  double velocityMean_;
  double velocitySigma_;
  OnlineMapMatchingStats stats_;

  bool initialize(const GPSPoint& gpsPoint);
  void predict(double deltaTime);
  void predictPath(
    int firstEdgeId,
    int currentEdgeId,
    double previousPathLength,
    double currentPathLength,
    double pathProbability,
    double sourceWeight,
    double deltaTime,
    std::unordered_map<int, double>& predicted
  );
  bool observeAndSelect(
    const GPSPoint& gpsPoint,
    OnlineMatchedPoint& matchedPoint
  );
  void updateVelocity(const GPSPoint& gpsPoint, double deltaTime);
  double observationProbability(const Point2D& point, int edgeId) const;
  double pathIntervalProbability(
    int firstEdgeId,
    double previousPathLength,
    double currentPathLength,
    double deltaTime
  ) const;
  double pathBeyondProbability(
    int firstEdgeId,
    double currentPathLength,
    double deltaTime
  ) const;
};

#endif
