#include "trace_bitcodec.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

const uint32_t kCompressedMagic = 0x54524331U;  // TRC1
const uint32_t kOriginalMagic = 0x54524f31U;    // TRO1
const uint8_t kPacketVersion = 3;
const uint64_t kDecodeLimit = 100000000ULL;

class BitWriter {
public:
  BitWriter() : bitCount_(0) { }

  void writeBit(bool value) {
    if ((bitCount_ & 7U) == 0) {
      bytes_.push_back(0);
    }
    if (value) {
      bytes_.back() |= (uint8_t)(1U << (7U - (bitCount_ & 7U)));
    }
    ++bitCount_;
  }

  void writeBits(uint64_t value, size_t width) {
    for (size_t i = width; i > 0; --i) {
      writeBit(((value >> (i - 1)) & 1ULL) != 0);
    }
  }

  void writeUnsignedExpGolomb(uint64_t value) {
    if (value == std::numeric_limits<uint64_t>::max()) {
      throw std::runtime_error("TRACE bitstream value overflow.");
    }
    const uint64_t codeNum = value + 1;
    size_t order = 0;
    for (uint64_t valueCopy = codeNum; valueCopy > 1; valueCopy >>= 1) {
      ++order;
    }
    for (size_t i = 0; i < order; ++i) {
      writeBit(false);
    }
    writeBits(codeNum, order + 1);
  }

  void writeSignedExpGolomb(int64_t value) {
    const uint64_t mapped = value >= 0
      ? (uint64_t)value * 2ULL
      : (uint64_t)(-(value + 1)) * 2ULL + 1ULL;
    writeUnsignedExpGolomb(mapped);
  }

  void writeDouble(double value) {
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    writeBits(raw, 64);
  }

  size_t bitCount() const { return bitCount_; }
  const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
  std::vector<uint8_t> bytes_;
  size_t bitCount_;
};

class BitReader {
public:
  explicit BitReader(const std::vector<uint8_t>& bytes)
    : bytes_(bytes), bitOffset_(0) { }

  bool readBit() {
    require(1);
    const bool value = ((bytes_.at(bitOffset_ >> 3) >> (7U - (bitOffset_ & 7U))) & 1U) != 0;
    ++bitOffset_;
    return value;
  }

  uint64_t readBits(size_t width) {
    if (width > 64) {
      throw std::runtime_error("TRACE bitstream field is too wide.");
    }
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) {
      value = (value << 1) | (readBit() ? 1ULL : 0ULL);
    }
    return value;
  }

  uint64_t readUnsignedExpGolomb() {
    size_t leadingZeros = 0;
    while (!readBit()) {
      if (++leadingZeros >= 63) {
        throw std::runtime_error("TRACE bitstream Exp-Golomb value overflow.");
      }
    }
    const uint64_t suffix = leadingZeros == 0 ? 0 : readBits(leadingZeros);
    return ((1ULL << leadingZeros) | suffix) - 1;
  }

  int64_t readSignedExpGolomb() {
    const uint64_t mapped = readUnsignedExpGolomb();
    if ((mapped & 1ULL) == 0) {
      return (int64_t)(mapped >> 1);
    }
    return -(int64_t)(mapped >> 1) - 1;
  }

  double readDouble() {
    const uint64_t raw = readBits(64);
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

private:
  void require(size_t width) const {
    if (width > bytes_.size() * 8U - bitOffset_) {
      throw std::runtime_error("TRACE bitstream is truncated.");
    }
  }

  const std::vector<uint8_t>& bytes_;
  size_t bitOffset_;
};

void writeHeader(BitWriter& writer, uint32_t magic) {
  writer.writeBits(magic, 32);
  writer.writeBits(kPacketVersion, 8);
}

int checkedInt(uint64_t value, const char* field);
size_t checkedCount(uint64_t value, const char* field);

size_t unsignedWidth(uint64_t value) {
  size_t width = 1;
  while (value > 1) {
    value >>= 1;
    ++width;
  }
  return width;
}

size_t terminatedReferenceWidth(
  const TraceCodec* references,
  int refId,
  const char* field
) {
  if (references == NULL) {
    throw std::runtime_error(
      std::string("TRACE ") + field + " len=0 requires synchronized reference state."
    );
  }
  const TraceReference* reference = references->findReferenceForDecoding(refId);
  if (reference == NULL || reference->edges.empty()) {
    throw std::runtime_error(
      std::string("TRACE ") + field + " len=0 reference is unavailable."
    );
  }
  return unsignedWidth((uint64_t)reference->edges.size());
}

size_t packedSpatialFactorCount(const std::vector<TraceSegment>& segments) {
  size_t count = 0;
  for (size_t i = 0; i < segments.size(); ++i) {
    ++count;
    if (segments.at(i).isReference && i + 1 < segments.size() &&
        !segments.at(i + 1).isReference) {
      ++i;
    }
  }
  return count;
}

size_t packedSpeedFactorCount(const std::vector<TraceSpeedFactor>& factors) {
  size_t count = 0;
  for (size_t i = 0; i < factors.size(); ++i) {
    ++count;
    if (factors.at(i).isReference && i + 1 < factors.size() &&
        !factors.at(i + 1).isReference) {
      ++i;
    }
  }
  return count;
}


void writeTemporal(
  BitWriter& writer,
  const TraceUtcqRepresentation& temporal,
  bool includeSharedMetadata = true
) {
  writer.writeUnsignedExpGolomb((uint64_t)temporal.size());
  if (temporal.size() == 0) {
    return;
  }
  if (temporal.growthRateCodes.size() + 1 != temporal.size() ||
      temporal.sampleIntervalDeviations.size() + 1 != temporal.size() ||
      temporal.gamma == 0 || temporal.gamma > 30) {
    throw std::runtime_error("TRACE UTCQ representation has inconsistent lengths.");
  }
  if (includeSharedMetadata) {
    writer.writeBits((uint64_t)(temporal.gamma - 1), 5);
  }
  writer.writeSignedExpGolomb(temporal.firstTimestamp);
  if (includeSharedMetadata) {
    writer.writeUnsignedExpGolomb(temporal.firstEdgeLengthMillimeters);
  }
  writer.writeBits(temporal.firstRelativeDistanceCode, temporal.gamma);
  writer.writeUnsignedExpGolomb(temporal.defaultSampleInterval);
  writer.writeUnsignedExpGolomb((uint64_t)packedSpeedFactorCount(temporal.speedFactors));
  size_t representedSpeeds = 0;
  for (size_t i = 0; i < temporal.speedFactors.size(); ++i) {
    const TraceSpeedFactor& factor = temporal.speedFactors.at(i);
    writer.writeBit(factor.isReference);
    if (factor.isReference) {
      if (factor.refId < 0 || factor.offset < 0 || factor.length <= 0) {
        throw std::runtime_error("TRACE contains an invalid speed reference factor.");
      }
      writer.writeUnsignedExpGolomb((uint64_t)factor.refId);
      const size_t valueWidth = std::max(
        unsignedWidth((uint64_t)factor.offset),
        unsignedWidth((uint64_t)factor.length)
      );
      const size_t len = factor.referenceTerminated
        ? unsignedWidth((uint64_t)factor.referenceLength) : valueWidth;
      if (len < valueWidth || len > 31) {
        throw std::runtime_error("TRACE terminated speed reference length is invalid.");
      }
      writer.writeUnsignedExpGolomb(factor.referenceTerminated ? 0U : (uint64_t)len);
      writer.writeBits((uint64_t)factor.offset, len);
      writer.writeBits((uint64_t)factor.length, len);
      representedSpeeds += (size_t)factor.length;
      const bool hasMismatch = i + 1 < temporal.speedFactors.size() &&
        !temporal.speedFactors.at(i + 1).isReference;
      writer.writeBit(hasMismatch);
      if (hasMismatch) {
        const TraceSpeedFactor& mismatch = temporal.speedFactors.at(++i);
        writer.writeBits(mismatch.mismatchGrowthRateCode, temporal.gamma);
        ++representedSpeeds;
      }
    } else {
      writer.writeBits(factor.mismatchGrowthRateCode, temporal.gamma);
      ++representedSpeeds;
    }
  }
  if (representedSpeeds != temporal.growthRateCodes.size()) {
    throw std::runtime_error("TRACE speed factors do not cover the speed sequence.");
  }
  for (size_t i = 0; i < temporal.sampleIntervalDeviations.size(); ++i) {
    writer.writeSignedExpGolomb(temporal.sampleIntervalDeviations.at(i));
  }
}

void readTemporal(
  BitReader& reader,
  TraceUtcqRepresentation& temporal,
  const TraceCodec* speedReferences,
  unsigned int sharedGamma = 0,
  uint64_t sharedFirstEdgeLengthMillimeters = 0
) {
  const size_t count = checkedCount(reader.readUnsignedExpGolomb(), "temporal count");
  temporal = TraceUtcqRepresentation();
  if (count == 0) {
    return;
  }
  temporal.gamma = sharedGamma == 0
    ? (unsigned int)reader.readBits(5) + 1U : sharedGamma;
  temporal.firstTimestamp = reader.readSignedExpGolomb();
  temporal.firstEdgeLengthMillimeters = sharedFirstEdgeLengthMillimeters == 0
    ? reader.readUnsignedExpGolomb() : sharedFirstEdgeLengthMillimeters;
  temporal.firstRelativeDistanceCode = reader.readBits(temporal.gamma);
  temporal.defaultSampleInterval = reader.readUnsignedExpGolomb();
  if (temporal.firstEdgeLengthMillimeters == 0 || temporal.defaultSampleInterval == 0) {
    throw std::runtime_error("TRACE UTCQ representation has invalid metadata.");
  }
  const size_t factorCount = checkedCount(reader.readUnsignedExpGolomb(), "speed factor count");
  size_t representedSpeeds = 0;
  temporal.speedFactors.reserve(factorCount * 2U);
  temporal.growthRateCodes.assign(count - 1, 0);
  for (size_t i = 0; i < factorCount; ++i) {
    TraceSpeedFactor factor;
    factor.isReference = reader.readBit();
    if (factor.isReference) {
      factor.refId = checkedInt(reader.readUnsignedExpGolomb(), "speed reference id");
      const size_t encodedLen = checkedCount(
        reader.readUnsignedExpGolomb(), "speed S/L bit length"
      );
      const size_t len = encodedLen == 0
        ? terminatedReferenceWidth(speedReferences, factor.refId, "speed")
        : encodedLen;
      if (len == 0 || len > 31) {
        throw std::runtime_error("TRACE speed factor S/L width is invalid.");
      }
      factor.referenceTerminated = encodedLen == 0;
      if (factor.referenceTerminated) {
        const TraceReference* reference =
          speedReferences->findReferenceForDecoding(factor.refId);
        factor.referenceLength = reference->edges.size();
      }
      factor.offset = checkedInt(reader.readBits(len), "speed reference offset");
      factor.length = checkedInt(reader.readBits(len), "speed reference length");
      if (factor.length <= 0) {
        throw std::runtime_error("TRACE contains an empty speed reference factor.");
      }
      representedSpeeds += (size_t)factor.length;
      temporal.speedFactors.push_back(factor);
      if (reader.readBit()) {
        TraceSpeedFactor mismatch;
        mismatch.mismatchGrowthRateCode = reader.readBits(temporal.gamma);
        if (representedSpeeds >= count - 1) {
          throw std::runtime_error("TRACE speed mismatch length overflow.");
        }
        temporal.growthRateCodes.at(representedSpeeds++) = mismatch.mismatchGrowthRateCode;
        temporal.speedFactors.push_back(mismatch);
      }
    } else {
      factor.mismatchGrowthRateCode = reader.readBits(temporal.gamma);
      if (representedSpeeds >= count - 1) {
        throw std::runtime_error("TRACE speed factor length overflow.");
      }
      temporal.growthRateCodes.at(representedSpeeds++) = factor.mismatchGrowthRateCode;
      temporal.speedFactors.push_back(factor);
    }
  }
  if (representedSpeeds != count - 1) {
    throw std::runtime_error("TRACE speed factor length mismatch.");
  }
  temporal.sampleIntervalDeviations.reserve(count - 1);
  for (size_t i = 1; i < count; ++i) {
    temporal.sampleIntervalDeviations.push_back(reader.readSignedExpGolomb());
  }
}

int checkedInt(uint64_t value, const char* field) {
  if (value > (uint64_t)std::numeric_limits<int>::max()) {
    throw std::runtime_error(std::string("TRACE bitstream invalid ") + field + ".");
  }
  return (int)value;
}

size_t checkedCount(uint64_t value, const char* field) {
  if (value > kDecodeLimit || value > (uint64_t)std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(std::string("TRACE bitstream invalid ") + field + ".");
  }
  return (size_t)value;
}

}  // namespace

TraceBinaryPacket::TraceBinaryPacket()
  : meaningfulBits(0), framingBits(0), spatialBits(0), temporalBits(0) { }

size_t TraceBinaryPacket::fileBits() const {
  return bytes.size() * 8U;
}

TraceStreamPacket::TraceStreamPacket()
  : meaningfulBits(0) { }

size_t TraceStreamPacket::fileBits() const {
  return bytes.size() * 8U;
}

TraceBitCodec::TraceBitCodec(unsigned int outgoingEdgeBits)
  : outgoingEdgeBits_(outgoingEdgeBits) {
  if (outgoingEdgeBits_ == 0 || outgoingEdgeBits_ > 30) {
    throw std::runtime_error("TRACE outgoing-edge width is invalid.");
  }
}

TraceBinaryPacket TraceBitCodec::encodeCompressed(
  const TraceCompressedTrajectory& compressed,
  const TraceUtcqRepresentation& temporal
) const {
  BitWriter writer;
  writeHeader(writer, kCompressedMagic);
  writer.writeUnsignedExpGolomb((uint64_t)(compressed.startNodeId + 1));
  writer.writeUnsignedExpGolomb((uint64_t)compressed.originalSpatialLength);
  writer.writeBits((uint64_t)(outgoingEdgeBits_ - 1), 5);
  writer.writeUnsignedExpGolomb((uint64_t)packedSpatialFactorCount(compressed.spatialSegments));
  const size_t framingEnd = writer.bitCount();

  for (size_t i = 0; i < compressed.spatialSegments.size(); ++i) {
    const TraceSegment& segment = compressed.spatialSegments.at(i);
    writer.writeBit(segment.isReference);
    if (segment.isReference) {
      if (segment.refId < 0 || segment.offset < 0 || segment.length <= 0) {
        throw std::runtime_error("TRACE cannot encode an invalid reference factor.");
      }
      writer.writeUnsignedExpGolomb((uint64_t)segment.refId);
      const size_t valueWidth = std::max(
        unsignedWidth((uint64_t)segment.offset),
        unsignedWidth((uint64_t)segment.length)
      );
      const size_t len = segment.referenceTerminated
        ? unsignedWidth((uint64_t)segment.referenceLength) : valueWidth;
      if (len < valueWidth || len > 31) {
        throw std::runtime_error("TRACE terminated spatial reference length is invalid.");
      }
      writer.writeUnsignedExpGolomb(segment.referenceTerminated ? 0U : (uint64_t)len);
      writer.writeBits((uint64_t)segment.offset, len);
      writer.writeBits((uint64_t)segment.length, len);
      const bool hasMismatch = i + 1 < compressed.spatialSegments.size() &&
        !compressed.spatialSegments.at(i + 1).isReference;
      writer.writeBit(hasMismatch);
      if (hasMismatch) {
        const int mismatch = compressed.spatialSegments.at(++i).literalEdge;
        if (mismatch < 0 || (uint64_t)mismatch >= (1ULL << outgoingEdgeBits_)) {
          throw std::runtime_error("TRACE outgoing mismatch exceeds its fixed width.");
        }
        writer.writeBits((uint64_t)mismatch, outgoingEdgeBits_);
      }
    } else {
      if (segment.literalEdge < 0) {
        throw std::runtime_error("TRACE cannot encode a negative outgoing edge number.");
      }
      if ((uint64_t)segment.literalEdge >= (1ULL << outgoingEdgeBits_)) {
        throw std::runtime_error("TRACE outgoing literal exceeds its fixed width.");
      }
      writer.writeBits((uint64_t)segment.literalEdge, outgoingEdgeBits_);
    }
  }
  const size_t spatialEnd = writer.bitCount();
  writeTemporal(writer, temporal);

  TraceBinaryPacket packet;
  packet.bytes = writer.bytes();
  packet.meaningfulBits = writer.bitCount();
  packet.framingBits = framingEnd;
  packet.spatialBits = spatialEnd - framingEnd;
  packet.temporalBits = writer.bitCount() - spatialEnd;
  return packet;
}

TraceBinaryPacket TraceBitCodec::encodePaperCompressed(
  const TraceCompressedTrajectory& compressed,
  const TraceUtcqRepresentation& temporal,
  const std::vector<unsigned int>& spatialValueWidths
) const {
  if (spatialValueWidths.size() != compressed.originalSpatialLength) {
    throw std::runtime_error("TRACE paper spatial widths do not match the trajectory.");
  }
  BitWriter writer;
  writer.writeUnsignedExpGolomb((uint64_t)(compressed.startNodeId + 1));
  writer.writeUnsignedExpGolomb(
    (uint64_t)packedSpatialFactorCount(compressed.spatialSegments)
  );
  const size_t framingEnd = writer.bitCount();

  size_t spatialPosition = 0;
  for (size_t i = 0; i < compressed.spatialSegments.size(); ++i) {
    const TraceSegment& segment = compressed.spatialSegments.at(i);
    writer.writeBit(segment.isReference);
    if (segment.isReference) {
      if (segment.refId < 0 || segment.offset < 0 || segment.length <= 0) {
        throw std::runtime_error("TRACE cannot encode an invalid paper reference factor.");
      }
      writer.writeUnsignedExpGolomb((uint64_t)segment.refId);
      const size_t valueWidth = std::max(
        unsignedWidth((uint64_t)segment.offset),
        unsignedWidth((uint64_t)segment.length)
      );
      const size_t len = segment.referenceTerminated
        ? unsignedWidth((uint64_t)segment.referenceLength) : valueWidth;
      if (len < valueWidth || len > 31) {
        throw std::runtime_error("TRACE paper spatial reference length is invalid.");
      }
      writer.writeUnsignedExpGolomb(segment.referenceTerminated ? 0U : (uint64_t)len);
      writer.writeBits((uint64_t)segment.offset, len);
      writer.writeBits((uint64_t)segment.length, len);
      spatialPosition += (size_t)segment.length;
      const bool hasMismatch = i + 1 < compressed.spatialSegments.size() &&
        !compressed.spatialSegments.at(i + 1).isReference;
      writer.writeBit(hasMismatch);
      if (hasMismatch) {
        const int mismatch = compressed.spatialSegments.at(++i).literalEdge;
        const unsigned int width = spatialValueWidths.at(spatialPosition++);
        if (mismatch < 0 ||
            (width < 64U && (uint64_t)mismatch >= (1ULL << width))) {
          throw std::runtime_error("TRACE paper outgoing mismatch exceeds its width.");
        }
        writer.writeBits((uint64_t)mismatch, width);
      }
    } else {
      const unsigned int width = spatialValueWidths.at(spatialPosition++);
      if (segment.literalEdge < 0 ||
          (width < 64U && (uint64_t)segment.literalEdge >= (1ULL << width))) {
        throw std::runtime_error("TRACE paper outgoing literal exceeds its width.");
      }
      writer.writeBits((uint64_t)segment.literalEdge, width);
    }
  }
  if (spatialPosition != spatialValueWidths.size()) {
    throw std::runtime_error("TRACE paper factors do not cover the spatial trajectory.");
  }
  const size_t spatialEnd = writer.bitCount();
  writeTemporal(writer, temporal, false);

  TraceBinaryPacket packet;
  packet.bytes = writer.bytes();
  packet.meaningfulBits = writer.bitCount();
  packet.framingBits = framingEnd;
  packet.spatialBits = spatialEnd - framingEnd;
  packet.temporalBits = writer.bitCount() - spatialEnd;
  return packet;
}

TraceBinaryPacket TraceBitCodec::encodeOriginal(
  int startNodeId,
  const std::vector<int>& encodedSpatial,
  const TraceUtcqRepresentation& temporal
) const {
  BitWriter writer;
  writeHeader(writer, kOriginalMagic);
  writer.writeUnsignedExpGolomb((uint64_t)(startNodeId + 1));
  writer.writeUnsignedExpGolomb((uint64_t)encodedSpatial.size());
  writer.writeBits((uint64_t)(outgoingEdgeBits_ - 1), 5);
  const size_t framingEnd = writer.bitCount();
  for (size_t i = 0; i < encodedSpatial.size(); ++i) {
    if (encodedSpatial.at(i) < 0) {
      throw std::runtime_error("TRACE cannot encode a negative original edge value.");
    }
    if ((uint64_t)encodedSpatial.at(i) >= (1ULL << outgoingEdgeBits_)) {
      throw std::runtime_error("TRACE original outgoing edge exceeds its fixed width.");
    }
    writer.writeBits((uint64_t)encodedSpatial.at(i), outgoingEdgeBits_);
  }
  const size_t spatialEnd = writer.bitCount();
  writeTemporal(writer, temporal);

  TraceBinaryPacket packet;
  packet.bytes = writer.bytes();
  packet.meaningfulBits = writer.bitCount();
  packet.framingBits = framingEnd;
  packet.spatialBits = spatialEnd - framingEnd;
  packet.temporalBits = writer.bitCount() - spatialEnd;
  return packet;
}

void TraceBitCodec::decodeCompressed(
  const std::vector<uint8_t>& bytes,
  TraceCompressedTrajectory& compressed,
  TraceUtcqRepresentation& temporal,
  const TraceCodec* spatialReferences,
  const TraceCodec* speedReferences
) const {
  BitReader reader(bytes);
  if ((uint32_t)reader.readBits(32) != kCompressedMagic ||
      (uint8_t)reader.readBits(8) != kPacketVersion) {
    throw std::runtime_error("TRACE bitstream header mismatch.");
  }

  compressed = TraceCompressedTrajectory();
  const uint64_t encodedStartNode = reader.readUnsignedExpGolomb();
  compressed.startNodeId = encodedStartNode == 0 ? -1 : checkedInt(encodedStartNode - 1, "start node");
  compressed.originalSpatialLength = checkedCount(reader.readUnsignedExpGolomb(), "spatial length");
  const unsigned int outgoingBits = (unsigned int)reader.readBits(5) + 1U;
  const size_t factorCount = checkedCount(reader.readUnsignedExpGolomb(), "factor count");
  compressed.spatialSegments.reserve(factorCount * 2U);

  size_t reconstructedLength = 0;
  for (size_t i = 0; i < factorCount; ++i) {
    if (reader.readBit()) {
      const int refId = checkedInt(reader.readUnsignedExpGolomb(), "reference id");
      const size_t encodedLen = checkedCount(
        reader.readUnsignedExpGolomb(), "S/L bit length"
      );
      const size_t len = encodedLen == 0
        ? terminatedReferenceWidth(spatialReferences, refId, "spatial")
        : encodedLen;
      if (len == 0 || len > 31) {
        throw std::runtime_error("TRACE factor S/L width is invalid.");
      }
      const int offset = checkedInt(reader.readBits(len), "reference offset");
      const int length = checkedInt(reader.readBits(len), "reference length");
      if (length <= 0) {
        throw std::runtime_error("TRACE bitstream contains an empty reference factor.");
      }
      TraceSegment factor = TraceSegment::reference(refId, offset, length);
      factor.referenceTerminated = encodedLen == 0;
      if (factor.referenceTerminated) {
        const TraceReference* reference =
          spatialReferences->findReferenceForDecoding(refId);
        factor.referenceLength = reference->edges.size();
      }
      compressed.spatialSegments.push_back(factor);
      reconstructedLength += (size_t)length;
      compressed.matchedEdgeCount += (size_t)length;
      if (reader.readBit()) {
        const int mismatch = checkedInt(reader.readBits(outgoingBits), "outgoing mismatch");
        if (mismatch < 0) {
          throw std::runtime_error("TRACE bitstream contains a negative outgoing mismatch.");
        }
        compressed.spatialSegments.push_back(TraceSegment::literal(mismatch));
        ++reconstructedLength;
        ++compressed.literalEdgeCount;
      }
    } else {
      const int literal = checkedInt(reader.readBits(outgoingBits), "literal");
      if (literal < 0) {
        throw std::runtime_error("TRACE bitstream contains a negative outgoing literal.");
      }
      compressed.spatialSegments.push_back(TraceSegment::literal(literal));
      ++reconstructedLength;
      ++compressed.literalEdgeCount;
    }
    if (reconstructedLength > compressed.originalSpatialLength) {
      throw std::runtime_error("TRACE bitstream spatial length overflow.");
    }
  }
  if (reconstructedLength != compressed.originalSpatialLength) {
    throw std::runtime_error("TRACE bitstream spatial length mismatch.");
  }

  readTemporal(reader, temporal, speedReferences);
}

void TraceBitCodec::decodePaperCompressed(
  const std::vector<uint8_t>& bytes,
  TraceCompressedTrajectory& compressed,
  TraceUtcqRepresentation& temporal,
  unsigned int gamma,
  uint64_t firstEdgeLengthMillimeters,
  const TraceCodec* spatialReferences,
  const TraceCodec* speedReferences
) const {
  if (gamma == 0 || gamma > 30 || firstEdgeLengthMillimeters == 0) {
    throw std::runtime_error("TRACE paper decoder requires shared temporal metadata.");
  }
  BitReader reader(bytes);
  compressed = TraceCompressedTrajectory();
  const uint64_t encodedStartNode = reader.readUnsignedExpGolomb();
  compressed.startNodeId = encodedStartNode == 0
    ? -1 : checkedInt(encodedStartNode - 1, "paper start node");
  const size_t factorCount = checkedCount(
    reader.readUnsignedExpGolomb(), "paper factor count"
  );
  compressed.spatialSegments.reserve(factorCount * 2U);

  size_t reconstructedLength = 0;
  for (size_t i = 0; i < factorCount; ++i) {
    if (reader.readBit()) {
      const int refId = checkedInt(
        reader.readUnsignedExpGolomb(), "paper reference id"
      );
      const size_t encodedLen = checkedCount(
        reader.readUnsignedExpGolomb(), "paper S/L bit length"
      );
      const size_t len = encodedLen == 0
        ? terminatedReferenceWidth(spatialReferences, refId, "paper spatial")
        : encodedLen;
      if (len == 0 || len > 31) {
        throw std::runtime_error("TRACE paper factor S/L width is invalid.");
      }
      const int offset = checkedInt(reader.readBits(len), "paper reference offset");
      const int length = checkedInt(reader.readBits(len), "paper reference length");
      if (length <= 0) {
        throw std::runtime_error("TRACE paper bitstream has an empty reference factor.");
      }
      TraceSegment factor = TraceSegment::reference(refId, offset, length);
      factor.referenceTerminated = encodedLen == 0;
      if (factor.referenceTerminated) {
        const TraceReference* reference =
          spatialReferences->findReferenceForDecoding(refId);
        factor.referenceLength = reference->edges.size();
      }
      compressed.spatialSegments.push_back(factor);
      reconstructedLength += (size_t)length;
      compressed.matchedEdgeCount += (size_t)length;
      if (reader.readBit()) {
        const int mismatch = checkedInt(
          reader.readBits(outgoingEdgeBits_), "paper outgoing mismatch"
        );
        if (mismatch < 0) {
          throw std::runtime_error("TRACE paper bitstream has a negative mismatch.");
        }
        compressed.spatialSegments.push_back(TraceSegment::literal(mismatch));
        ++reconstructedLength;
        ++compressed.literalEdgeCount;
      }
    } else {
      const int literal = checkedInt(
        reader.readBits(outgoingEdgeBits_), "paper outgoing literal"
      );
      if (literal < 0) {
        throw std::runtime_error("TRACE paper bitstream has a negative literal.");
      }
      compressed.spatialSegments.push_back(TraceSegment::literal(literal));
      ++reconstructedLength;
      ++compressed.literalEdgeCount;
    }
  }
  compressed.originalSpatialLength = reconstructedLength;
  readTemporal(
    reader,
    temporal,
    speedReferences,
    gamma,
    firstEdgeLengthMillimeters
  );
}

TraceStreamPacket TraceBitCodec::encodePaperStream(
  const std::vector<TraceBinaryPacket>& packets
) const {
  BitWriter writer;
  for (size_t packetIndex = 0; packetIndex < packets.size(); ++packetIndex) {
    const TraceBinaryPacket& packet = packets.at(packetIndex);
    if (packet.bytes.size() * 8U < packet.meaningfulBits) {
      throw std::runtime_error("TRACE paper stream has an invalid packet length.");
    }
    BitReader packetReader(packet.bytes);
    for (size_t bit = 0; bit < packet.meaningfulBits; ++bit) {
      writer.writeBit(packetReader.readBit());
    }
  }

  TraceStreamPacket stream;
  stream.bytes = writer.bytes();
  stream.meaningfulBits = writer.bitCount();
  return stream;
}
