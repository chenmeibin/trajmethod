#include "online_map_matcher.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "../vendor_press/util/helper.hpp"

namespace {

const double kPi = 3.14159265358979323846;
const double kSqrtThree = 1.7320508075688772935;

double logistic(double value) {
  if (value >= 0.0) {
    const double decay = std::exp(-value);
    return 1.0 / (1.0 + decay);
  }
  const double growth = std::exp(value);
  return growth / (1.0 + growth);
}

double scaledSoftplus(double value, double scale) {
  const double normalized = value / scale;
  if (normalized > 40.0) {
    return value;
  }
  if (normalized < -40.0) {
    return scale * std::exp(normalized);
  }
  return scale * std::log1p(std::exp(normalized));
}

double clampProbability(double value) {
  return std::max(0.0, std::min(1.0, value));
}

}  // namespace

OnlineMapMatchingParameters::OnlineMapMatchingParameters()
  : initialSearchRadius(30.0),
    predictionThreshold(0.00001),
    updateThreshold(0.001),
    accelerationSigma(3.0),
    observationSigma(5.0),
    velocityObservationSigma(30.0 / 3.6) { }

OnlineMatchedPoint::OnlineMatchedPoint()
  : edgeId(-1),
    networkDistanceOnEdge(0.0),
    timestamp(0.0),
    posteriorProbability(0.0) { }

OnlineMapMatchingStats::OnlineMapMatchingStats()
  : pointsProcessed(0),
    pointsMatched(0),
    initializations(0),
    predictionCandidates(0),
    prunedCandidates(0),
    predictionBranches(0) { }

OnlineMapMatcher::OnlineMapMatcher(
  const Graph& graph,
  const GridIndex& gridIndex,
  const OnlineMapMatchingParameters& parameters
) : graph_(graph),
    gridIndex_(gridIndex),
    parameters_(parameters),
    hasPreviousGps_(false),
    hasVelocityEstimate_(false),
    previousGps_(0.0, 0.0, 0.0),
    velocityMean_(0.0),
    velocitySigma_(parameters.velocityObservationSigma) {
  if (parameters_.initialSearchRadius <= 0.0 ||
      parameters_.predictionThreshold <= 0.0 ||
      parameters_.updateThreshold <= 0.0 ||
      parameters_.accelerationSigma <= 0.0 ||
      parameters_.observationSigma <= 0.0 ||
      parameters_.velocityObservationSigma <= 0.0) {
    throw std::runtime_error("Online map matching parameters are invalid.");
  }
}

void OnlineMapMatcher::reset() {
  candidates_.clear();
  hasPreviousGps_ = false;
  hasVelocityEstimate_ = false;
  velocityMean_ = 0.0;
  velocitySigma_ = parameters_.velocityObservationSigma;
}

const OnlineMapMatchingStats& OnlineMapMatcher::getStats() const {
  return stats_;
}

size_t OnlineMapMatcher::getCandidateCount() const {
  return candidates_.size();
}

bool OnlineMapMatcher::initialize(const GPSPoint& gpsPoint) {
  const Point2D point = gps2Point2D(gpsPoint);
  std::unordered_set<int> nearby;
  gridIndex_.search(point, parameters_.initialSearchRadius, nearby);
  candidates_.clear();
  for (std::unordered_set<int>::const_iterator it = nearby.begin();
       it != nearby.end(); ++it) {
    if (*it < 0 || *it >= (int)graph_.getEdgeNumber()) {
      continue;
    }
    const Edge& edge = graph_.getEdge((size_t)*it);
    if (distPoint2Polyline(point, edge.getShape()) >=
        parameters_.initialSearchRadius) {
      continue;
    }
    const double prior = edge.getDistance();
    if (prior > 0.0) {
      candidates_.push_back(Candidate(*it, prior));
    }
  }
  ++stats_.initializations;
  return !candidates_.empty();
}

double OnlineMapMatcher::pathIntervalProbability(
  int firstEdgeId,
  double previousPathLength,
  double currentPathLength,
  double deltaTime
) const {
  const double previousBeyond = previousPathLength <= 0.0
    ? 1.0
    : pathBeyondProbability(firstEdgeId, previousPathLength, deltaTime);
  const double currentBeyond = pathBeyondProbability(
    firstEdgeId,
    currentPathLength,
    deltaTime
  );
  return clampProbability(previousBeyond - currentBeyond);
}

double OnlineMapMatcher::pathBeyondProbability(
  int firstEdgeId,
  double currentPathLength,
  double deltaTime
) const {
  if (deltaTime <= 0.0) {
    return 0.0;
  }
  const double firstLength = graph_.getEdge((size_t)firstEdgeId).getDistance();
  if (firstLength <= 0.0) {
    return 0.0;
  }
  const double scale = kSqrtThree * velocitySigma_ * deltaTime / kPi;
  const double meanDistance = velocityMean_ * deltaTime;
  const double upper = scaledSoftplus(
    meanDistance - (currentPathLength - firstLength),
    scale
  );
  const double lower = scaledSoftplus(
    meanDistance - currentPathLength,
    scale
  );
  return clampProbability((upper - lower) / firstLength);
}

void OnlineMapMatcher::predictPath(
  int firstEdgeId,
  int currentEdgeId,
  double previousPathLength,
  double currentPathLength,
  double pathProbability,
  double sourceWeight,
  double deltaTime,
  std::unordered_map<int, double>& predicted
) {
  if (pathProbability <= 0.0) {
    return;
  }
  const double intervalProbability = pathIntervalProbability(
    firstEdgeId,
    previousPathLength,
    currentPathLength,
    deltaTime
  );
  predicted[currentEdgeId] += sourceWeight * pathProbability * intervalProbability;
  ++stats_.predictionBranches;

  const double beyond = pathBeyondProbability(
    firstEdgeId,
    currentPathLength,
    deltaTime
  );
  if (sourceWeight * pathProbability * beyond <=
      parameters_.predictionThreshold) {
    return;
  }

  const Edge& edge = graph_.getEdge((size_t)currentEdgeId);
  const Node& target = graph_.getNode((size_t)edge.getTargetId());
  if (target.getEdgeNumber() == 0) {
    return;
  }
  const double transition = 1.0 / (double)target.getEdgeNumber();
  for (size_t i = 0; i < target.getEdgeNumber(); ++i) {
    const int nextEdgeId = target.getEdgeId(i);
    const double nextLength = currentPathLength +
      graph_.getEdge((size_t)nextEdgeId).getDistance();
    predictPath(
      firstEdgeId,
      nextEdgeId,
      currentPathLength,
      nextLength,
      pathProbability * transition,
      sourceWeight,
      deltaTime,
      predicted
    );
  }
}

void OnlineMapMatcher::predict(double deltaTime) {
  std::unordered_map<int, double> predicted;
  for (size_t i = 0; i < candidates_.size(); ++i) {
    const int edgeId = candidates_.at(i).edgeId;
    const double edgeLength = graph_.getEdge((size_t)edgeId).getDistance();
    predictPath(
      edgeId,
      edgeId,
      0.0,
      edgeLength,
      1.0,
      candidates_.at(i).weight,
      deltaTime,
      predicted
    );
  }
  candidates_.clear();
  candidates_.reserve(predicted.size());
  for (std::unordered_map<int, double>::const_iterator it = predicted.begin();
       it != predicted.end(); ++it) {
    if (it->second > 0.0) {
      candidates_.push_back(Candidate(it->first, it->second));
    }
  }
  stats_.predictionCandidates += candidates_.size();
}

double OnlineMapMatcher::observationProbability(
  const Point2D& point,
  int edgeId
) const {
  const Edge& edge = graph_.getEdge((size_t)edgeId);
  const std::vector<Point2D>& shape = edge.getShape();
  if (shape.size() < 2) {
    return 0.0;
  }
  const double edgeLength = edge.getDistance();
  if (edgeLength <= 0.0) {
    return 0.0;
  }
  const double distance = distPoint2Polyline(point, shape);
  const double projected = std::max(
    0.0,
    std::min(edgeLength, distProjAlongGeo(point, shape))
  );
  const double logisticScale = kSqrtThree * parameters_.observationSigma / kPi;
  const double endpointCorrection =
    logistic((edgeLength - projected) / logisticScale) -
    logistic(-projected / logisticScale);
  return endpointCorrection * std::exp(
    -0.5 * distance * distance /
    (parameters_.observationSigma * parameters_.observationSigma)
  ) / edgeLength;
}

void OnlineMapMatcher::updateVelocity(
  const GPSPoint& gpsPoint,
  double deltaTime
) {
  if (!hasPreviousGps_ || deltaTime <= 0.0) {
    return;
  }
  const double observedVelocity = euclideanDistance(
    gps2Point2D(previousGps_),
    gps2Point2D(gpsPoint)
  ) / deltaTime;
  const double predictedVariance = velocitySigma_ * velocitySigma_ +
    parameters_.accelerationSigma * parameters_.accelerationSigma *
    deltaTime * deltaTime;
  const double observationVariance =
    parameters_.velocityObservationSigma *
    parameters_.velocityObservationSigma;
  velocityMean_ = (
    observationVariance * velocityMean_ +
    predictedVariance * observedVelocity
  ) / (observationVariance + predictedVariance);
  velocitySigma_ = std::sqrt(
    1.0 / (1.0 / observationVariance + 1.0 / predictedVariance)
  );
}

bool OnlineMapMatcher::observeAndSelect(
  const GPSPoint& gpsPoint,
  OnlineMatchedPoint& matchedPoint
) {
  if (candidates_.empty()) {
    return false;
  }
  const Point2D point = gps2Point2D(gpsPoint);
  double total = 0.0;
  for (size_t i = 0; i < candidates_.size(); ++i) {
    candidates_.at(i).weight *= observationProbability(
      point,
      candidates_.at(i).edgeId
    );
    total += candidates_.at(i).weight;
  }
  if (total <= std::numeric_limits<double>::min()) {
    return false;
  }
  for (size_t i = 0; i < candidates_.size(); ++i) {
    candidates_.at(i).weight /= total;
  }
  std::vector<Candidate> retained;
  retained.reserve(candidates_.size());
  for (size_t i = 0; i < candidates_.size(); ++i) {
    if (candidates_.at(i).weight > parameters_.updateThreshold) {
      retained.push_back(candidates_.at(i));
    } else {
      ++stats_.prunedCandidates;
    }
  }
  candidates_.swap(retained);
  if (candidates_.empty()) {
    return false;
  }
  const Candidate* best = &candidates_.front();
  for (size_t i = 1; i < candidates_.size(); ++i) {
    if (candidates_.at(i).weight > best->weight) {
      best = &candidates_.at(i);
    }
  }
  matchedPoint.edgeId = best->edgeId;
  matchedPoint.timestamp = gpsPoint.t;
  matchedPoint.posteriorProbability = best->weight;
  matchedPoint.networkDistanceOnEdge = distProjAlongGeo(
    point,
    graph_.getEdge((size_t)best->edgeId).getShape()
  );
  ++stats_.pointsMatched;
  return true;
}

bool OnlineMapMatcher::push(
  const GPSPoint& gpsPoint,
  OnlineMatchedPoint& matchedPoint
) {
  ++stats_.pointsProcessed;
  const double deltaTime = hasPreviousGps_
    ? std::max(0.0, gpsPoint.t - previousGps_.t)
    : 0.0;
  if (!hasPreviousGps_ && !initialize(gpsPoint)) {
    previousGps_ = gpsPoint;
    hasPreviousGps_ = true;
    return false;
  }
  if (candidates_.empty()) {
    previousGps_ = gpsPoint;
    return false;
  }
  if (hasPreviousGps_) {
    if (!hasVelocityEstimate_ && deltaTime > 0.0) {
      velocityMean_ = euclideanDistance(
        gps2Point2D(previousGps_),
        gps2Point2D(gpsPoint)
      ) / deltaTime;
      velocitySigma_ = parameters_.velocityObservationSigma;
      hasVelocityEstimate_ = true;
      predict(deltaTime);
    } else {
      predict(deltaTime);
      updateVelocity(gpsPoint, deltaTime);
    }
  }
  if (!observeAndSelect(gpsPoint, matchedPoint)) {
    candidates_.clear();
    previousGps_ = gpsPoint;
    hasPreviousGps_ = true;
    return false;
  }
  previousGps_ = gpsPoint;
  hasPreviousGps_ = true;
  return true;
}
