#include "trace_metrics.hpp"

#include <iomanip>
#include <sstream>

TraceRunMetrics::TraceRunMetrics()
  : totalGpsInputs(0),
    totalGpsPoints(0),
    skippedEmptyInputs(0),
    totalMatchedSegments(0),
    totalOriginalEdges(0),
    totalMatchedEdges(0),
    totalLiteralEdges(0),
    totalCompressedSpatialTokens(0),
    totalOriginalSpeeds(0),
    totalMatchedSpeeds(0),
    totalLiteralSpeeds(0),
    terminatedSpatialFactors(0),
    terminatedSpeedFactors(0),
    totalStreamArrivals(0),
    totalNonEmptyEdgeArrivals(0),
    onlineMapMatchingUpdates(0),
    onlineMapMatchingMatches(0),
    onlineMapMatchingInitializations(0),
    onlineMapMatchingPredictionCandidates(0),
    onlineMapMatchingPrunedCandidates(0),
    onlineMapMatchingPredictionBranches(0),
    totalOriginalSpatialBits(0),
    totalCompressedSpatialBits(0),
    totalOriginalTemporalBits(0),
    totalCompressedTemporalBits(0),
    totalOriginalFramingBits(0),
    totalCompressedFramingBits(0),
    totalOriginalBitstreamBytes(0),
    totalCompressedBitstreamBytes(0),
    outputBitstreamBytes(0),
    outputBitstreamMeaningfulBits(0),
    totalCompressionMillis(0.0) { }

namespace {

double ratioOrZero(size_t numerator, size_t denominator) {
  if (denominator == 0) {
    return 0.0;
  }
  return (double)numerator / (double)denominator;
}

double ratioOrZero(double numerator, size_t denominator) {
  if (denominator == 0) {
    return 0.0;
  }
  return numerator / (double)denominator;
}

}

std::string buildTraceSummaryJson(const TraceSummaryInput& input) {
  const TraceRunMetrics& metrics = input.metrics;

  // Match PRESS's logical input boundary: the map-matched spatial edge-ID
  // sequence plus the temporal (timestamp, network-distance) sequence.
  const size_t pressReadySpatialBytes = metrics.totalOriginalEdges * 4U;
  const size_t pressReadyTemporalBytes = metrics.totalStreamArrivals * 16U;
  const size_t pressReadyInputBytes = pressReadySpatialBytes + pressReadyTemporalBytes;
  const size_t finalOutputBytes = metrics.outputBitstreamBytes;

  std::ostringstream summary;
  summary << "{\n";
  summary << "  \"dataset\": \"" << input.dataset << "\",\n";
  summary << "  \"gps_inputs\": " << metrics.totalGpsInputs << ",\n";
  summary << "  \"gps_points\": " << metrics.totalGpsPoints << ",\n";
  summary << "  \"mapped_point_count_for_cr\": "
          << metrics.totalStreamArrivals << ",\n";
  summary << "  \"map_matching_point_retention\": "
          << ratioOrZero(metrics.totalStreamArrivals, metrics.totalGpsPoints) << ",\n";
  summary << "  \"online_map_matching\": {\n";
  summary << "    \"method\": \"Taguchi_route_prediction_untrained\",\n";
  summary << "    \"updates\": " << metrics.onlineMapMatchingUpdates << ",\n";
  summary << "    \"matches\": " << metrics.onlineMapMatchingMatches << ",\n";
  summary << "    \"initializations\": "
          << metrics.onlineMapMatchingInitializations << ",\n";
  summary << "    \"prediction_candidates\": "
          << metrics.onlineMapMatchingPredictionCandidates << ",\n";
  summary << "    \"pruned_candidates\": "
          << metrics.onlineMapMatchingPrunedCandidates << ",\n";
  summary << "    \"prediction_branches\": "
          << metrics.onlineMapMatchingPredictionBranches << "\n";
  summary << "  },\n";
  summary << "  \"cr_definition\": "
          << "\"map_matched_trajectory_logical_bytes / final_output_file_bytes\",\n";
  summary << "  \"compression_ratio\": "
          << ratioOrZero(pressReadyInputBytes, finalOutputBytes) << ",\n";
  summary << "  \"map_matched_trajectory_schema\": "
          << "\"int32 edge_id sequence + (float64 timestamp,float64 network_distance) sequence\",\n";
  summary << "  \"map_matched_spatial_bytes\": " << pressReadySpatialBytes << ",\n";
  summary << "  \"map_matched_temporal_bytes\": " << pressReadyTemporalBytes << ",\n";
  summary << "  \"map_matched_input_logical_bytes\": " << pressReadyInputBytes << ",\n";
  summary << "  \"final_output_format\": \"trace_online_bitstream\",\n";
  summary << "  \"final_output_file_bytes\": " << finalOutputBytes << ",\n";
  summary << "  \"skipped_empty_inputs\": " << metrics.skippedEmptyInputs << ",\n";
  summary << "  \"matched_segments\": " << metrics.totalMatchedSegments << ",\n";
  summary << "  \"original_spatial_edges\": " << metrics.totalOriginalEdges << ",\n";
  summary << "  \"matched_reference_edges\": " << metrics.totalMatchedEdges << ",\n";
  summary << "  \"literal_edges\": " << metrics.totalLiteralEdges << ",\n";
  summary << "  \"compressed_spatial_tokens\": " << metrics.totalCompressedSpatialTokens << ",\n";
  summary << "  \"original_speed_values\": " << metrics.totalOriginalSpeeds << ",\n";
  summary << "  \"matched_reference_speeds\": " << metrics.totalMatchedSpeeds << ",\n";
  summary << "  \"literal_growth_rates\": " << metrics.totalLiteralSpeeds << ",\n";
  summary << "  \"terminated_spatial_factors_len_zero\": "
          << metrics.terminatedSpatialFactors << ",\n";
  summary << "  \"terminated_speed_factors_len_zero\": "
          << metrics.terminatedSpeedFactors << ",\n";
  summary << "  \"speed_reference_coverage\": "
          << ratioOrZero(metrics.totalMatchedSpeeds, metrics.totalOriginalSpeeds) << ",\n";
  summary << "  \"stream_arrivals\": " << metrics.totalStreamArrivals << ",\n";
  summary << "  \"non_empty_edge_arrivals\": " << metrics.totalNonEmptyEdgeArrivals << ",\n";
  summary << "  \"spatial_compression_ratio\": " << std::fixed << std::setprecision(6)
          << ratioOrZero(metrics.totalOriginalEdges, metrics.totalCompressedSpatialTokens) << ",\n";
  summary << "  \"original_spatial_bits\": " << metrics.totalOriginalSpatialBits << ",\n";
  summary << "  \"compressed_spatial_bits\": " << metrics.totalCompressedSpatialBits << ",\n";
  summary << "  \"spatial_bit_compression_ratio\": "
          << ratioOrZero(metrics.totalOriginalSpatialBits, metrics.totalCompressedSpatialBits) << ",\n";
  summary << "  \"original_temporal_bits\": " << metrics.totalOriginalTemporalBits << ",\n";
  summary << "  \"compressed_temporal_bits\": " << metrics.totalCompressedTemporalBits << ",\n";
  summary << "  \"temporal_bit_compression_ratio\": "
          << ratioOrZero(metrics.totalOriginalTemporalBits, metrics.totalCompressedTemporalBits) << ",\n";
  summary << "  \"original_framing_bits\": " << metrics.totalOriginalFramingBits << ",\n";
  summary << "  \"compressed_framing_bits\": " << metrics.totalCompressedFramingBits << ",\n";
  summary << "  \"original_bitstream_bytes\": " << metrics.totalOriginalBitstreamBytes << ",\n";
  summary << "  \"compressed_bitstream_bytes\": " << metrics.totalCompressedBitstreamBytes << ",\n";
  summary << "  \"output_bitstream_meaningful_bits\": "
          << metrics.outputBitstreamMeaningfulBits << ",\n";
  summary << "  \"matched_edge_coverage\": "
          << ratioOrZero(metrics.totalMatchedEdges, metrics.totalOriginalEdges) << ",\n";
  summary << "  \"compression_time_ms\": " << metrics.totalCompressionMillis << ",\n";
  summary << "  \"average_compression_time_ms\": "
          << ratioOrZero(metrics.totalCompressionMillis, metrics.totalMatchedSegments) << ",\n";
  summary << "  \"reference_count\": " << input.referenceCount << ",\n";
  summary << "  \"reference_edges\": " << input.referenceEdges << ",\n";
  summary << "  \"speed_reference_count\": " << input.speedReferenceCount << ",\n";
  summary << "  \"speed_reference_values\": " << input.speedReferenceValues << ",\n";
  summary << "  \"runtime_stats\": {\n";
  summary << "    \"trajectories_compressed\": " << input.runtimeStats.trajectoriesCompressed << ",\n";
  summary << "    \"kmer_lookups\": " << input.runtimeStats.kmerLookups << ",\n";
  summary << "    \"reference_candidates_visited\": " << input.runtimeStats.refCandidatesVisited << ",\n";
  summary << "    \"edge_comparisons\": " << input.runtimeStats.edgeComparisons << ",\n";
  summary << "    \"rewrite_candidates_evaluated\": " << input.runtimeStats.rewriteCandidatesEvaluated << ",\n";
  summary << "    \"reference_hits\": " << input.runtimeStats.referenceHits << ",\n";
  summary << "    \"literal_outputs\": " << input.runtimeStats.literalOutputs << ",\n";
  summary << "    \"references_added\": " << input.runtimeStats.referencesAdded << ",\n";
  summary << "    \"references_rewritten\": " << input.runtimeStats.referencesRewritten << ",\n";
  summary << "    \"references_deleted\": " << input.runtimeStats.referencesDeleted << ",\n";
  summary << "    \"max_reference_count\": " << input.runtimeStats.maxReferenceCount << ",\n";
  summary << "    \"max_reference_edges\": " << input.runtimeStats.maxReferenceEdges << "\n";
  summary << "  },\n";
  summary << "  \"trace_parameters\": {\n";
  summary << "    \"k_mer_length\": " << input.traceParameters.kMerLength << ",\n";
  summary << "    \"freshness_decay\": " << input.traceParameters.freshnessDecay << ",\n";
  summary << "    \"deletion_threshold\": " << input.traceParameters.deletionThreshold << ",\n";
  summary << "    \"rewrite_alpha\": " << input.traceParameters.rewriteAlpha << "\n";
  summary << "  },\n";
  summary << "  \"speed_representation_parameters\": {\n";
  summary << "    \"gamma\": " << input.encodingGamma << ",\n";
  summary << "    \"eta\": " << input.speedErrorEta << "\n";
  summary << "  }\n";
  summary << "}\n";
  return summary.str();
}
