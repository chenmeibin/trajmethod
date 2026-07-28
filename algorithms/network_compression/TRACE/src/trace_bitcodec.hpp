#ifndef TRACE_TRACE_BITCODEC_HPP
#define TRACE_TRACE_BITCODEC_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "trace_codec.hpp"
#include "utcq.hpp"

struct TraceBinaryPacket {
  std::vector<uint8_t> bytes;
  size_t meaningfulBits;
  size_t framingBits;
  size_t spatialBits;
  size_t temporalBits;

  TraceBinaryPacket();
  size_t fileBits() const;
};

struct TraceStreamPacket {
  std::vector<uint8_t> bytes;
  size_t meaningfulBits;

  TraceStreamPacket();
  size_t fileBits() const;
};

// Bit-level packet codec used by TRACE transmission. Reference contents are
// shared state; packets carry reference factors and UTCQ-compressed time pairs.
class TraceBitCodec {
public:
  explicit TraceBitCodec(unsigned int outgoingEdgeBits = 1);

  TraceBinaryPacket encodeCompressed(
    const TraceCompressedTrajectory& compressed,
    const TraceUtcqRepresentation& temporal
  ) const;
  TraceBinaryPacket encodePaperCompressed(
    const TraceCompressedTrajectory& compressed,
    const TraceUtcqRepresentation& temporal,
    const std::vector<unsigned int>& spatialValueWidths
  ) const;

  TraceBinaryPacket encodeOriginal(
    int startNodeId,
    const std::vector<int>& encodedSpatial,
    const TraceUtcqRepresentation& temporal
  ) const;

  void decodeCompressed(
    const std::vector<uint8_t>& bytes,
    TraceCompressedTrajectory& compressed,
    TraceUtcqRepresentation& temporal,
    const TraceCodec* spatialReferences = NULL,
    const TraceCodec* speedReferences = NULL
  ) const;
  void decodePaperCompressed(
    const std::vector<uint8_t>& bytes,
    TraceCompressedTrajectory& compressed,
    TraceUtcqRepresentation& temporal,
    unsigned int gamma,
    uint64_t firstEdgeLengthMillimeters,
    const TraceCodec* spatialReferences,
    const TraceCodec* speedReferences
  ) const;
  TraceStreamPacket encodePaperStream(
    const std::vector<TraceBinaryPacket>& packets
  ) const;
private:
  unsigned int outgoingEdgeBits_;
};

#endif
