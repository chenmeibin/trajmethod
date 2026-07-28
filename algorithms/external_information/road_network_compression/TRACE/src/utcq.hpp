#ifndef TRACE_UTCQ_HPP
#define TRACE_UTCQ_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../vendor_press/topology/trajectory.hpp"

// Parameters explicitly used by TRACE: gamma is the fixed precision of RD/GD
// (Section 5.1), and eta is the speed matching error bound (Section 4.2).
struct UtcqTemporalParameters {
  unsigned int gamma;
  double eta;

  UtcqTemporalParameters();
  UtcqTemporalParameters(unsigned int gamma, double eta);
};

struct TraceSpeedFactor {
  bool isReference;
  int refId;
  int offset;
  int length;
  uint64_t mismatchGrowthRateCode;
  bool referenceTerminated;
  size_t referenceLength;

  TraceSpeedFactor();
};

// TRACE Section 4.1 representation of ad(Tr) and its stated
// T=<t0,(delta t)-Ts> time sequence. All mapped points are retained.
struct TraceUtcqRepresentation {
  unsigned int gamma;
  int64_t firstTimestamp;
  uint64_t defaultSampleInterval;
  uint64_t firstEdgeLengthMillimeters;
  uint64_t firstRelativeDistanceCode;
  std::vector<uint64_t> growthRateCodes;
  std::vector<int64_t> sampleIntervalDeviations;
  std::vector<double> speeds;
  std::vector<int64_t> quantizedSpeeds;
  std::vector<TraceSpeedFactor> speedFactors;
  std::vector<TemporalPair> reconstructedTemporal;

  TraceUtcqRepresentation();
  size_t size() const;
};

class UtcqCodec {
public:
  explicit UtcqCodec(const UtcqTemporalParameters& parameters = UtcqTemporalParameters());

  const UtcqTemporalParameters& getParameters() const;
  TraceUtcqRepresentation buildTraceRepresentation(
    const std::vector<TemporalPair>& temporal,
    double firstEdgeLength,
    double defaultSampleInterval
  ) const;
  void reconstructTemporal(
    TraceUtcqRepresentation& representation,
    const std::vector<double>& referencedSpeeds,
    const std::vector<bool>& usesReferencedSpeed
  ) const;

private:
  UtcqTemporalParameters parameters_;
};

#endif
