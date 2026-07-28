#include "utcq.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

UtcqTemporalParameters::UtcqTemporalParameters()
  : gamma(3), eta(3.0) { }

UtcqTemporalParameters::UtcqTemporalParameters(unsigned int gammaValue, double etaValue)
  : gamma(gammaValue), eta(etaValue) {
  if (gamma == 0 || gamma > 30) {
    gamma = 3;
  }
  if (!std::isfinite(eta) || eta < 0.0) {
    eta = 3.0;
  }
}

TraceSpeedFactor::TraceSpeedFactor()
  : isReference(false),
    refId(-1),
    offset(0),
    length(1),
    mismatchGrowthRateCode(0),
    referenceTerminated(false),
    referenceLength(0) { }

TraceUtcqRepresentation::TraceUtcqRepresentation()
  : gamma(3),
    firstTimestamp(0),
    defaultSampleInterval(1),
    firstEdgeLengthMillimeters(1),
    firstRelativeDistanceCode(0) { }

size_t TraceUtcqRepresentation::size() const {
  return reconstructedTemporal.size();
}

UtcqCodec::UtcqCodec(const UtcqTemporalParameters& parameters)
  : parameters_(parameters) { }

const UtcqTemporalParameters& UtcqCodec::getParameters() const {
  return parameters_;
}

namespace {

uint64_t positiveMillimeters(double value) {
  if (!std::isfinite(value) || value <= 0.0) {
    return 1;
  }
  return (uint64_t)std::max(1.0, std::floor(value * 1000.0 + 0.5));
}

int64_t roundedSeconds(double value) {
  if (!std::isfinite(value) ||
      value >= (double)std::numeric_limits<int64_t>::max() ||
      value <= (double)std::numeric_limits<int64_t>::min()) {
    return 0;
  }
  return (int64_t)std::llround(value);
}

uint64_t encodeFraction(double value, unsigned int gamma) {
  const uint64_t scale = 1ULL << gamma;
  if (!std::isfinite(value) || value <= 0.0) {
    return 0;
  }
  return std::min(scale - 1, (uint64_t)std::llround(value * (double)scale));
}

double decodeFraction(uint64_t code, unsigned int gamma) {
  return (double)code / (double)(1ULL << gamma);
}

}  // namespace

TraceUtcqRepresentation UtcqCodec::buildTraceRepresentation(
  const std::vector<TemporalPair>& temporal,
  double firstEdgeLength,
  double defaultSampleInterval
) const {
  TraceUtcqRepresentation result;
  result.gamma = parameters_.gamma;
  if (temporal.empty()) {
    return result;
  }

  result.firstEdgeLengthMillimeters = positiveMillimeters(firstEdgeLength);
  const double decodedFirstEdgeLength =
    (double)result.firstEdgeLengthMillimeters / 1000.0;
  result.firstTimestamp = roundedSeconds(temporal.front().t);
  result.defaultSampleInterval = (uint64_t)std::max<int64_t>(
    1,
    roundedSeconds(defaultSampleInterval)
  );

  // Definition 4 and Section 4.1: RD is the first location's distance
  // relative to the length of its outgoing edge.
  const double rd = temporal.front().dist / decodedFirstEdgeLength;
  result.firstRelativeDistanceCode = encodeFraction(rd, result.gamma);

  for (size_t i = 1; i < temporal.size(); ++i) {
    const double delta = std::max(0.0, temporal.at(i).dist - temporal.at(i - 1).dist);
    double gd = 0.0;
    if (i == 1) {
      if (temporal.at(i).dist > 0.0) {
        gd = delta / temporal.at(i).dist;
      }
    } else {
      const double denominator = temporal.at(i).dist - temporal.at(i - 2).dist;
      if (denominator > 0.0) {
        gd = delta / denominator;
      }
    }
    result.growthRateCodes.push_back(encodeFraction(gd, result.gamma));

    const double dt = temporal.at(i).t - temporal.at(i - 1).t;
    const double speed = dt > 0.0 ? delta / dt : 0.0;
    result.speeds.push_back(speed);
    const double speedUnit = 0.5 * parameters_.eta;
    result.quantizedSpeeds.push_back(speedUnit > 0.0
      ? (int64_t)std::llround(speed / speedUnit)
      : (int64_t)std::llround(speed * 1000.0));

    const int64_t interval = roundedSeconds(temporal.at(i).t) -
      roundedSeconds(temporal.at(i - 1).t);
    result.sampleIntervalDeviations.push_back(
      interval - (int64_t)result.defaultSampleInterval
    );
  }

  result.speedFactors.reserve(result.growthRateCodes.size());
  for (size_t i = 0; i < result.growthRateCodes.size(); ++i) {
    TraceSpeedFactor factor;
    factor.mismatchGrowthRateCode = result.growthRateCodes.at(i);
    result.speedFactors.push_back(factor);
  }
  std::vector<double> noSpeeds(result.growthRateCodes.size(), 0.0);
  std::vector<bool> noReferences(result.growthRateCodes.size(), false);
  reconstructTemporal(result, noSpeeds, noReferences);
  return result;
}

void UtcqCodec::reconstructTemporal(
  TraceUtcqRepresentation& representation,
  const std::vector<double>& referencedSpeeds,
  const std::vector<bool>& usesReferencedSpeed
) const {
  if (referencedSpeeds.size() != representation.growthRateCodes.size() ||
      usesReferencedSpeed.size() != representation.growthRateCodes.size()) {
    throw std::runtime_error("TRACE speed reconstruction length mismatch.");
  }
  representation.reconstructedTemporal.clear();
  double distance = decodeFraction(
    representation.firstRelativeDistanceCode,
    representation.gamma
  ) * ((double)representation.firstEdgeLengthMillimeters / 1000.0);
  int64_t timestamp = representation.firstTimestamp;
  representation.reconstructedTemporal.push_back(TemporalPair((double)timestamp, distance));
  double previousGrowth = 0.0;
  for (size_t i = 0; i < representation.growthRateCodes.size(); ++i) {
    const int64_t interval = (int64_t)representation.defaultSampleInterval +
      representation.sampleIntervalDeviations.at(i);
    double growth = 0.0;
    if (usesReferencedSpeed.at(i)) {
      growth = std::max(0.0, referencedSpeeds.at(i) * (double)interval);
    } else {
      const double gd = decodeFraction(representation.growthRateCodes.at(i), representation.gamma);
      if (i == 0 && gd < 1.0) {
        const double nextDistance = distance / (1.0 - gd);
        growth = std::max(0.0, nextDistance - distance);
      } else if (i > 0 && gd < 1.0) {
        growth = gd * previousGrowth / (1.0 - gd);
      }
    }
    distance += growth;
    previousGrowth = growth;
    timestamp += interval;
    representation.reconstructedTemporal.push_back(TemporalPair((double)timestamp, distance));
  }
}
