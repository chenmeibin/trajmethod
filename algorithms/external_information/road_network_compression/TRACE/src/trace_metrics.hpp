#ifndef TRACE_TRACE_METRICS_HPP
#define TRACE_TRACE_METRICS_HPP

#include <cstddef>
#include <string>

#include "trace_codec.hpp"

struct TraceRunMetrics {
  size_t totalGpsInputs;
  size_t totalGpsPoints;
  size_t skippedEmptyInputs;
  size_t totalMatchedSegments;
  size_t totalOriginalEdges;
  size_t totalMatchedEdges;
  size_t totalLiteralEdges;
  size_t totalCompressedSpatialTokens;
  size_t totalOriginalSpeeds;
  size_t totalMatchedSpeeds;
  size_t totalLiteralSpeeds;
  size_t terminatedSpatialFactors;
  size_t terminatedSpeedFactors;
  size_t totalStreamArrivals;
  size_t totalNonEmptyEdgeArrivals;
  size_t onlineMapMatchingUpdates;
  size_t onlineMapMatchingMatches;
  size_t onlineMapMatchingInitializations;
  size_t onlineMapMatchingPredictionCandidates;
  size_t onlineMapMatchingPrunedCandidates;
  size_t onlineMapMatchingPredictionBranches;
  size_t totalOriginalSpatialBits;
  size_t totalCompressedSpatialBits;
  size_t totalOriginalTemporalBits;
  size_t totalCompressedTemporalBits;
  size_t totalOriginalFramingBits;
  size_t totalCompressedFramingBits;
  size_t totalOriginalBitstreamBytes;
  size_t totalCompressedBitstreamBytes;
  size_t outputBitstreamBytes;
  size_t outputBitstreamMeaningfulBits;
  double totalCompressionMillis;

  TraceRunMetrics();
};

struct TraceSummaryInput {
  std::string dataset;
  TraceRunMetrics metrics;
  TraceCompressionOptions traceParameters;
  TraceRuntimeStats runtimeStats;
  size_t referenceCount;
  size_t referenceEdges;
  size_t speedReferenceCount;
  size_t speedReferenceValues;
  unsigned int encodingGamma;
  double speedErrorEta;
};

std::string buildTraceSummaryJson(const TraceSummaryInput& input);

#endif
