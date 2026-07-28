#ifndef TRACE_TRACE_CODEC_HPP
#define TRACE_TRACE_CODEC_HPP

#include <cstdint>
#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../vendor_press/topology/trajectory.hpp"

struct TraceSegment {
  bool isReference;
  int refId;
  int offset;
  int length;
  int literalEdge;
  bool referenceTerminated;
  size_t referenceLength;

  TraceSegment();
  static TraceSegment reference(int refId, int offset, int length);
  static TraceSegment literal(int edgeId);
};

struct TraceCompressedTrajectory {
  std::vector<TraceSegment> spatialSegments;
  std::vector<TemporalPair> temporalComponent;
  size_t originalSpatialLength;
  size_t matchedEdgeCount;
  size_t literalEdgeCount;
  int startNodeId;
  std::vector<std::vector<int> > literalCandidates;

  TraceCompressedTrajectory();
};

struct TraceBlockStats {
  int startOffset;
  int length;
  std::vector<uint32_t> flowAccess;

  TraceBlockStats();
};

struct TraceRuntimeStats {
  size_t trajectoriesCompressed;
  size_t kmerLookups;
  size_t refCandidatesVisited;
  size_t edgeComparisons;
  size_t rewriteCandidatesEvaluated;
  size_t referenceHits;
  size_t literalOutputs;
  size_t referencesAdded;
  size_t referencesRewritten;
  size_t referencesDeleted;
  size_t maxReferenceCount;
  size_t maxReferenceEdges;

  TraceRuntimeStats();
};

struct TraceReference {
  int refId;
  std::vector<int> edges;
  double freshness;
  size_t useCount;
  double lastTouchedTick;
  bool hasBeenRewritten;
  bool terminated;
  std::vector<std::unordered_map<uint64_t, uint32_t> > rewriteAssociations;
  std::vector<TraceBlockStats> blocks;

  TraceReference();
};

struct TraceCompressionOptions {
  int kMerLength;
  double freshnessDecay;
  double deletionThreshold;
  double rewriteAlpha;
  bool enableReferenceRewrite;

  TraceCompressionOptions();
};

class TraceCodec {
public:
  explicit TraceCodec(const TraceCompressionOptions& options = TraceCompressionOptions());

  void compressTrajectory(
    const PRESSTrajectory& input,
    TraceCompressedTrajectory& output
  );
  int beginOnlineTrajectory();
  void pushOnlineEdges(
    int trajectoryId,
    const std::vector<int>& arrivedEdges,
    double timestamp
  );
  void finishOnlineTrajectory(
    int trajectoryId,
    TraceCompressedTrajectory& output
  );
  void decompressTrajectory(
    const TraceCompressedTrajectory& input,
    PRESSTrajectory& output
  ) const;

  const std::vector<TraceReference>& getReferences() const;
  const TraceReference* findReferenceForDecoding(int refId) const;
  size_t getTotalReferenceEdges() const;
  const TraceRuntimeStats& getRuntimeStats() const;
  size_t getKMerIndexSize() const;
  size_t getKMerOccurrenceCount() const;
  size_t getPatternFrequencyCount() const;
  size_t getReferenceBlockCellCount() const;

private:
  struct RefOccurrence {
    int refId;
    int offset;
  };

  struct KMerEntry {
    uint64_t key;
    int refId;
    int offset;
    int pt;
    int pd;
    int collisionNext;
    bool active;

    KMerEntry();
  };

  struct OnlineTrajectoryState {
    enum Mode {
      UNDECIDED,
      REFERENCE,
      NON_REFERENCE
    };

    Mode mode;
    int referenceId;
    std::vector<int> pending;
    bool hasActiveFactor;
    int activeRefId;
    int activeOffset;
    int activeLength;
    bool hasPendingRewrite;
    int pendingRewriteRefId;
    int pendingRewritePosition;
    int pendingRewriteMismatch;
    TraceCompressedTrajectory output;

    OnlineTrajectoryState();
  };

  TraceCompressionOptions options_;
  std::vector<TraceReference> references_;
  std::unordered_map<int, size_t> refIdToIndex_;
  std::vector<int> kmerBucketHeads_;
  std::vector<KMerEntry> kmerEntries_;
  std::vector<int> freeKMerEntries_;
  std::unordered_map<int, int> kmerTailByRef_;
  std::unordered_map<uint64_t, int> kmerEntryByRefOffset_;
  size_t activeKMerEntries_;
  int nextRefId_;
  int nextOnlineTrajectoryId_;
  double currentTick_;
  double freshnessSum_;
  std::list<int> referenceFreshnessOrder_;
  std::unordered_map<int, std::list<int>::iterator> freshnessOrderByRef_;
  TraceRuntimeStats runtimeStats_;
  std::unordered_map<int, OnlineTrajectoryState> onlineTrajectories_;

  uint64_t buildKMerKey(const std::vector<int>& sequence, size_t start) const;
  uint64_t buildSeedKey(
    const std::vector<int>& sequence,
    size_t start,
    int length
  ) const;
  void collectKMerOccurrences(
    uint64_t key,
    std::vector<RefOccurrence>& occurrences
  ) const;
  double freshnessOf(const TraceReference& reference) const;
  void advanceFreshness(double timestamp);
  void registerReferenceFreshness(const TraceReference& reference);
  void touchReference(TraceReference& reference);
  void rebuildReferenceState();
  void insertReferenceKMer(int refId, int offset, uint64_t key);
  void removeReferenceKMer(int refId, int offset);
  void removeReferenceKMers(int refId);
  void ensureReferenceMetadata(TraceReference& reference);
  void updateReferenceUsage(
    int refId,
    int offset,
    int length,
    bool markVisited = true
  );
  bool findInitialKMerMatch(
    const std::vector<int>& sequence,
    int& refId,
    int& offset
  );
  void appendReferenceEdge(int refId, int edge);
  void rewriteReference(size_t refIndex, int position, int mismatch);
  const TraceReference* findReferenceSnapshot(int refId) const;
  void finalizeOnlineFactor(OnlineTrajectoryState& state);
  void recordOnlineMismatch(
    OnlineTrajectoryState& state,
    int mismatch
  );
  void confirmPendingRewrite(
    OnlineTrajectoryState& state,
    int nextRefId,
    int nextOffset
  );
  void processOnlineNonReferenceEdge(
    OnlineTrajectoryState& state,
    int edge
  );
  void compressSpatial(
    const std::vector<int>& spatial,
    std::vector<TraceSegment>& segments,
    size_t& matchedEdgeCount,
    size_t& literalEdgeCount,
    std::vector<std::vector<int> >& literalCandidates
  );
  void decompressSpatial(
    const std::vector<TraceSegment>& segments,
    std::vector<int>& spatial
  ) const;
  int addReference(const std::vector<int>& spatial, double freshnessBoost);
  void enforceReferenceDeletion();
};

#endif
