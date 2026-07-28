#include "trace_codec.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

TraceSegment::TraceSegment()
  : isReference(false), refId(-1), offset(0), length(1), literalEdge(0),
    referenceTerminated(false), referenceLength(0) { }

TraceSegment TraceSegment::reference(int refId, int offset, int length) {
  TraceSegment segment;
  segment.isReference = true;
  segment.refId = refId;
  segment.offset = offset;
  segment.length = length;
  return segment;
}

TraceSegment TraceSegment::literal(int edgeId) {
  TraceSegment segment;
  segment.literalEdge = edgeId;
  return segment;
}

TraceCompressedTrajectory::TraceCompressedTrajectory()
  : originalSpatialLength(0),
    matchedEdgeCount(0),
    literalEdgeCount(0),
    startNodeId(-1) { }

TraceBlockStats::TraceBlockStats() : startOffset(0), length(0) { }

TraceRuntimeStats::TraceRuntimeStats()
  : trajectoriesCompressed(0),
    kmerLookups(0),
    refCandidatesVisited(0),
    edgeComparisons(0),
    rewriteCandidatesEvaluated(0),
    referenceHits(0),
    literalOutputs(0),
    referencesAdded(0),
    referencesRewritten(0),
    referencesDeleted(0),
    maxReferenceCount(0),
    maxReferenceEdges(0) { }

TraceReference::TraceReference()
  : refId(-1),
    freshness(0.0),
    useCount(0),
    lastTouchedTick(0.0),
    hasBeenRewritten(false),
    terminated(false) { }

TraceCompressionOptions::TraceCompressionOptions()
  : kMerLength(7),
    freshnessDecay(0.998),
    deletionThreshold(0.5),
    rewriteAlpha(2.0),
    enableReferenceRewrite(true) { }

TraceCodec::KMerEntry::KMerEntry()
  : key(0), refId(-1), offset(0), pt(-1), pd(-1), collisionNext(-1), active(true) { }

TraceCodec::OnlineTrajectoryState::OnlineTrajectoryState()
  : mode(UNDECIDED),
    referenceId(-1),
    hasActiveFactor(false),
    activeRefId(-1),
    activeOffset(0),
    activeLength(0),
    hasPendingRewrite(false),
    pendingRewriteRefId(-1),
    pendingRewritePosition(0),
    pendingRewriteMismatch(0) { }

namespace {

const size_t kFixedHashCardinality = 1000U;

size_t totalReferenceEdges(const std::vector<TraceReference>& references) {
  size_t total = 0;
  for (size_t i = 0; i < references.size(); ++i) {
    total += references.at(i).edges.size();
  }
  return total;
}

uint64_t singleValueKey(int value) {
  return (uint64_t)(uint32_t)value + 1ULL;
}

size_t fvIndex(int b, int x, int y) {
  return (size_t)(x * b - x * (x - 1) / 2 + y - x);
}

bool shouldRewriteAt(
  const TraceReference& reference,
  int position,
  int mismatch,
  int k,
  double alpha
);

}  // namespace

TraceCodec::TraceCodec(const TraceCompressionOptions& options)
  : options_(options),
    nextRefId_(0),
    nextOnlineTrajectoryId_(0),
    currentTick_(0.0),
    freshnessSum_(0.0),
    activeKMerEntries_(0) {
  if (options_.kMerLength <= 0) {
    throw std::runtime_error("TRACE k must be positive.");
  }
  if (options_.freshnessDecay <= 0.0 || options_.freshnessDecay >= 1.0) {
    throw std::runtime_error("TRACE lambda must be in (0, 1).");
  }
  if (options_.deletionThreshold <= 0.0 || options_.deletionThreshold > 1.0) {
    throw std::runtime_error("TRACE C must be in (0, 1].");
  }
  if (options_.rewriteAlpha < 1.0) {
    throw std::runtime_error("TRACE alpha must be at least 1.");
  }
  rebuildReferenceState();
}

uint64_t TraceCodec::buildSeedKey(
  const std::vector<int>& sequence,
  size_t start,
  int length
) const {
  // BKDR hashing is the hash function explicitly selected in Section 4.3.
  uint64_t key = 0;
  for (int i = 0; i < length; ++i) {
    key = key * 131ULL + (uint64_t)(uint32_t)sequence.at(start + (size_t)i) + 1ULL;
  }
  return key;
}

uint64_t TraceCodec::buildKMerKey(
  const std::vector<int>& sequence,
  size_t start
) const {
  return buildSeedKey(sequence, start, options_.kMerLength);
}

void TraceCodec::collectKMerOccurrences(
  uint64_t key,
  std::vector<RefOccurrence>& occurrences
) const {
  occurrences.clear();
  if (kmerBucketHeads_.empty()) {
    return;
  }
  int entry = kmerBucketHeads_.at((size_t)(key % kmerBucketHeads_.size()));
  while (entry >= 0) {
    const KMerEntry& candidate = kmerEntries_.at((size_t)entry);
    if (candidate.active && candidate.key == key) {
      occurrences.push_back(RefOccurrence{candidate.refId, candidate.offset});
    }
    entry = candidate.collisionNext;
  }
}

void TraceCodec::rebuildReferenceState() {
  refIdToIndex_.clear();
  size_t occurrenceCount = 0;
  for (size_t i = 0; i < references_.size(); ++i) {
    refIdToIndex_[references_.at(i).refId] = i;
    if ((int)references_.at(i).edges.size() >= options_.kMerLength) {
      occurrenceCount += references_.at(i).edges.size() -
        (size_t)options_.kMerLength + 1U;
    }
    ensureReferenceMetadata(references_.at(i));
  }

  const size_t bucketCount = kFixedHashCardinality;
  kmerBucketHeads_.assign(bucketCount, -1);
  kmerEntries_.clear();
  kmerEntries_.reserve(occurrenceCount);
  freeKMerEntries_.clear();
  kmerTailByRef_.clear();
  kmerEntryByRefOffset_.clear();
  activeKMerEntries_ = 0;

  for (size_t refIndex = 0; refIndex < references_.size(); ++refIndex) {
    const TraceReference& reference = references_.at(refIndex);
    int previous = -1;
    for (size_t offset = 0;
         offset + (size_t)options_.kMerLength <= reference.edges.size();
         ++offset) {
      KMerEntry entry;
      entry.key = buildKMerKey(reference.edges, offset);
      entry.refId = reference.refId;
      entry.offset = (int)offset;
      entry.pt = previous;
      const size_t bucket = (size_t)(entry.key % bucketCount);
      entry.collisionNext = kmerBucketHeads_.at(bucket);
      const int current = (int)kmerEntries_.size();
      kmerEntries_.push_back(entry);
      kmerEntryByRefOffset_[
        ((uint64_t)(uint32_t)entry.refId << 32) | (uint32_t)entry.offset
      ] = current;
      ++activeKMerEntries_;
      kmerBucketHeads_.at(bucket) = current;
      if (previous >= 0) {
        kmerEntries_.at((size_t)previous).pd = current;
      }
      previous = current;
    }
    if (previous >= 0) {
      kmerTailByRef_[reference.refId] = previous;
    }
  }

  runtimeStats_.maxReferenceCount = std::max(
    runtimeStats_.maxReferenceCount,
    references_.size()
  );
  runtimeStats_.maxReferenceEdges = std::max(
    runtimeStats_.maxReferenceEdges,
    totalReferenceEdges(references_)
  );
}

void TraceCodec::insertReferenceKMer(int refId, int offset, uint64_t key) {
  if (kmerBucketHeads_.empty()) {
    kmerBucketHeads_.assign(kFixedHashCardinality, -1);
  }
  KMerEntry entry;
  entry.refId = refId;
  entry.offset = offset;
  entry.key = key;
  std::unordered_map<int, int>::const_iterator tail = kmerTailByRef_.find(refId);
  if (tail != kmerTailByRef_.end()) {
    entry.pt = tail->second;
  }
  const size_t bucket = (size_t)(key % kmerBucketHeads_.size());
  entry.collisionNext = kmerBucketHeads_.at(bucket);
  int current = -1;
  if (!freeKMerEntries_.empty()) {
    current = freeKMerEntries_.back();
    freeKMerEntries_.pop_back();
    kmerEntries_.at((size_t)current) = entry;
  } else {
    current = (int)kmerEntries_.size();
    kmerEntries_.push_back(entry);
  }
  kmerBucketHeads_.at(bucket) = current;
  if (entry.pt >= 0) {
    kmerEntries_.at((size_t)entry.pt).pd = current;
  }
  kmerTailByRef_[refId] = current;
  kmerEntryByRefOffset_[
    ((uint64_t)(uint32_t)refId << 32) | (uint32_t)offset
  ] = current;
  ++activeKMerEntries_;
}

void TraceCodec::removeReferenceKMers(int refId) {
  std::unordered_map<int, int>::iterator tail = kmerTailByRef_.find(refId);
  int entryIndex = tail == kmerTailByRef_.end() ? -1 : tail->second;
  while (entryIndex >= 0) {
    KMerEntry& entry = kmerEntries_.at((size_t)entryIndex);
    const int previous = entry.pt;
    const size_t bucket = (size_t)(entry.key % kmerBucketHeads_.size());
    int* link = &kmerBucketHeads_.at(bucket);
    while (*link >= 0 && *link != entryIndex) {
      link = &kmerEntries_.at((size_t)(*link)).collisionNext;
    }
    if (*link == entryIndex) {
      *link = entry.collisionNext;
    }
    entry.active = false;
    freeKMerEntries_.push_back(entryIndex);
    kmerEntryByRefOffset_.erase(
      ((uint64_t)(uint32_t)refId << 32) | (uint32_t)entry.offset
    );
    --activeKMerEntries_;
    entryIndex = previous;
  }
  kmerTailByRef_.erase(refId);
}

void TraceCodec::removeReferenceKMer(int refId, int offset) {
  const uint64_t identity =
    ((uint64_t)(uint32_t)refId << 32) | (uint32_t)offset;
  std::unordered_map<uint64_t, int>::iterator found =
    kmerEntryByRefOffset_.find(identity);
  if (found == kmerEntryByRefOffset_.end()) {
    return;
  }
  const int entryIndex = found->second;
  KMerEntry& entry = kmerEntries_.at((size_t)entryIndex);
  const size_t bucket = (size_t)(entry.key % kmerBucketHeads_.size());
  int* link = &kmerBucketHeads_.at(bucket);
  while (*link >= 0 && *link != entryIndex) {
    link = &kmerEntries_.at((size_t)(*link)).collisionNext;
  }
  if (*link == entryIndex) {
    *link = entry.collisionNext;
  }
  if (entry.pt >= 0) {
    kmerEntries_.at((size_t)entry.pt).pd = entry.pd;
  }
  if (entry.pd >= 0) {
    kmerEntries_.at((size_t)entry.pd).pt = entry.pt;
  } else if (entry.pt >= 0) {
    kmerTailByRef_[refId] = entry.pt;
  } else {
    kmerTailByRef_.erase(refId);
  }
  entry.active = false;
  freeKMerEntries_.push_back(entryIndex);
  kmerEntryByRefOffset_.erase(found);
  --activeKMerEntries_;
}

void TraceCodec::ensureReferenceMetadata(TraceReference& reference) {
  if (reference.hasBeenRewritten) {
    reference.rewriteAssociations.clear();
    reference.blocks.clear();
    return;
  }
  if (reference.rewriteAssociations.size() != reference.edges.size()) {
    reference.rewriteAssociations.assign(
      reference.edges.size(),
      std::unordered_map<uint64_t, uint32_t>()
    );
  }
  const int b = reference.edges.empty()
    ? 0
    : ((int)reference.edges.size() + options_.kMerLength - 1) / options_.kMerLength;
  if (b == 0) {
    reference.blocks.clear();
    return;
  }
  if (reference.blocks.size() != 1) {
    TraceBlockStats matrix;
    matrix.startOffset = 0;
    matrix.length = b;
    matrix.flowAccess.assign((size_t)b * (size_t)(b + 1) / 2U, 0);
    reference.blocks.assign(1, matrix);
    return;
  }
  if (reference.blocks.front().length != b) {
    const TraceBlockStats previous = reference.blocks.front();
    TraceBlockStats matrix;
    matrix.startOffset = 0;
    matrix.length = b;
    matrix.flowAccess.assign((size_t)b * (size_t)(b + 1) / 2U, 0);
    const int copied = std::min(previous.length, b);
    for (int x = 0; x < copied; ++x) {
      for (int y = x; y < copied; ++y) {
        matrix.flowAccess.at(fvIndex(b, x, y)) =
          previous.flowAccess.at(fvIndex(previous.length, x, y));
      }
    }
    reference.blocks.front() = matrix;
  }
}

double TraceCodec::freshnessOf(const TraceReference& reference) const {
  return std::pow(
    options_.freshnessDecay,
    std::max(0.0, currentTick_ - reference.lastTouchedTick)
  );
}

void TraceCodec::advanceFreshness(double timestamp) {
  const double nextTick = std::max(currentTick_, timestamp);
  if (nextTick > currentTick_ && !references_.empty()) {
    freshnessSum_ *= std::pow(options_.freshnessDecay, nextTick - currentTick_);
  }
  currentTick_ = nextTick;
}

void TraceCodec::registerReferenceFreshness(const TraceReference& reference) {
  referenceFreshnessOrder_.push_back(reference.refId);
  std::list<int>::iterator position = referenceFreshnessOrder_.end();
  --position;
  freshnessOrderByRef_[reference.refId] = position;
  freshnessSum_ += freshnessOf(reference);
}

void TraceCodec::touchReference(TraceReference& reference) {
  const double previousFreshness = freshnessOf(reference);
  freshnessSum_ += 1.0 - previousFreshness;
  reference.lastTouchedTick = currentTick_;
  reference.freshness = 1.0;
  std::unordered_map<int, std::list<int>::iterator>::iterator order =
    freshnessOrderByRef_.find(reference.refId);
  if (order != freshnessOrderByRef_.end()) {
    referenceFreshnessOrder_.splice(
      referenceFreshnessOrder_.end(),
      referenceFreshnessOrder_,
      order->second
    );
    std::list<int>::iterator position = referenceFreshnessOrder_.end();
    --position;
    order->second = position;
  }
}

void TraceCodec::updateReferenceUsage(
  int refId,
  int offset,
  int length,
  bool markVisited
) {
  std::unordered_map<int, size_t>::iterator found = refIdToIndex_.find(refId);
  if (found == refIdToIndex_.end()) {
    return;
  }
  TraceReference& reference = references_.at(found->second);
  if (markVisited) {
    touchReference(reference);
  }
  ++reference.useCount;
  ++runtimeStats_.referenceHits;
  ensureReferenceMetadata(reference);
  if (reference.blocks.empty()) {
    return;
  }
  TraceBlockStats& matrix = reference.blocks.front();
  const int b = matrix.length;
  const int x = offset / options_.kMerLength;
  const int y = (offset + length + options_.kMerLength - 1) /
    options_.kMerLength - 1;
  if (x >= 0 && x < b && y >= x && y < b) {
    ++matrix.flowAccess.at(fvIndex(b, x, y));
  }
}

const TraceReference* TraceCodec::findReferenceSnapshot(int refId) const {
  std::unordered_map<int, size_t>::const_iterator live = refIdToIndex_.find(refId);
  if (live != refIdToIndex_.end()) {
    return &references_.at(live->second);
  }
  return NULL;
}

void TraceCodec::rewriteReference(size_t refIndex, int position, int mismatch) {
  TraceReference& reference = references_.at(refIndex);
  const int firstOffset = std::max(0, position - options_.kMerLength + 1);
  const int lastOffset = std::min(
    position,
    (int)reference.edges.size() - options_.kMerLength
  );
  for (int offset = firstOffset; offset <= lastOffset; ++offset) {
    removeReferenceKMer(reference.refId, offset);
  }
  reference.edges.at((size_t)position) = mismatch;
  reference.hasBeenRewritten = true;
  reference.blocks.clear();
  reference.rewriteAssociations.clear();
  for (int offset = firstOffset; offset <= lastOffset; ++offset) {
    insertReferenceKMer(
      reference.refId,
      offset,
      buildKMerKey(reference.edges, (size_t)offset)
    );
  }
  ++runtimeStats_.referencesRewritten;
}

bool TraceCodec::findInitialKMerMatch(
  const std::vector<int>& sequence,
  int& refId,
  int& offset
) {
  refId = -1;
  offset = 0;
  if ((int)sequence.size() < options_.kMerLength) {
    return false;
  }
  ++runtimeStats_.kmerLookups;
  std::vector<RefOccurrence> occurrences;
  collectKMerOccurrences(buildKMerKey(sequence, 0), occurrences);
  int bestLength = -1;
  for (size_t i = 0; i < occurrences.size(); ++i) {
    ++runtimeStats_.refCandidatesVisited;
    std::unordered_map<int, size_t>::const_iterator found =
      refIdToIndex_.find(occurrences.at(i).refId);
    if (found == refIdToIndex_.end()) {
      continue;
    }
    const TraceReference& reference = references_.at(found->second);
    const int available = std::min(
      (int)sequence.size(),
      (int)reference.edges.size() - occurrences.at(i).offset
    );
    int matched = 0;
    while (matched < available) {
      ++runtimeStats_.edgeComparisons;
      if (reference.edges.at((size_t)(occurrences.at(i).offset + matched)) !=
          sequence.at((size_t)matched)) {
        break;
      }
      ++matched;
    }
    if (matched >= options_.kMerLength && matched > bestLength) {
      bestLength = matched;
      refId = occurrences.at(i).refId;
      offset = occurrences.at(i).offset;
    }
  }
  return bestLength >= options_.kMerLength;
}

void TraceCodec::appendReferenceEdge(int refId, int edge) {
  std::unordered_map<int, size_t>::iterator found = refIdToIndex_.find(refId);
  if (found == refIdToIndex_.end()) {
    return;
  }
  TraceReference& reference = references_.at(found->second);
  reference.edges.push_back(edge);
  touchReference(reference);
  ensureReferenceMetadata(reference);
  if ((int)reference.edges.size() < options_.kMerLength) {
    return;
  }

  const int offset = (int)reference.edges.size() - options_.kMerLength;
  insertReferenceKMer(refId, offset, buildKMerKey(reference.edges, (size_t)offset));
  runtimeStats_.maxReferenceEdges = std::max(
    runtimeStats_.maxReferenceEdges,
    totalReferenceEdges(references_)
  );
}

void TraceCodec::finalizeOnlineFactor(OnlineTrajectoryState& state) {
  if (!state.hasActiveFactor) {
    return;
  }
  TraceSegment factor = TraceSegment::reference(
    state.activeRefId,
    state.activeOffset,
    state.activeLength
  );
  const TraceReference* snapshot = findReferenceSnapshot(state.activeRefId);
  if (snapshot != NULL && snapshot->terminated) {
    factor.referenceTerminated = true;
    factor.referenceLength = snapshot->edges.size();
  }
  state.output.spatialSegments.push_back(factor);
  state.output.matchedEdgeCount += (size_t)state.activeLength;
  updateReferenceUsage(
    state.activeRefId,
    state.activeOffset,
    state.activeLength,
    false
  );
  state.hasActiveFactor = false;
  state.activeRefId = -1;
  state.activeOffset = 0;
  state.activeLength = 0;
}

void TraceCodec::recordOnlineMismatch(
  OnlineTrajectoryState& state,
  int mismatch
) {
  const int refId = state.activeRefId;
  const int rewritePosition = state.activeOffset + state.activeLength;
  finalizeOnlineFactor(state);
  state.output.spatialSegments.push_back(TraceSegment::literal(mismatch));
  ++state.output.literalEdgeCount;
  ++runtimeStats_.literalOutputs;
  state.hasPendingRewrite = options_.enableReferenceRewrite;
  state.pendingRewriteRefId = refId;
  state.pendingRewritePosition = rewritePosition;
  state.pendingRewriteMismatch = mismatch;
}

void TraceCodec::confirmPendingRewrite(
  OnlineTrajectoryState& state,
  int nextRefId,
  int nextOffset
) {
  if (!state.hasPendingRewrite) {
    return;
  }
  const int refId = state.pendingRewriteRefId;
  const int position = state.pendingRewritePosition;
  const int mismatch = state.pendingRewriteMismatch;
  state.hasPendingRewrite = false;
  state.pendingRewriteRefId = -1;

  if (nextRefId != refId || nextOffset != position + 1) {
    return;
  }
  std::unordered_map<int, size_t>::iterator found = refIdToIndex_.find(refId);
  if (found == refIdToIndex_.end() || position <= 0 ||
      position >= (int)references_.at(found->second).edges.size() - 1) {
    return;
  }
  TraceReference& reference = references_.at(found->second);
  ++reference.rewriteAssociations.at((size_t)position)[singleValueKey(mismatch)];
  ++runtimeStats_.rewriteCandidatesEvaluated;
  if (shouldRewriteAt(
        reference,
        position,
        mismatch,
        options_.kMerLength,
        options_.rewriteAlpha
      )) {
    rewriteReference(found->second, position, mismatch);
  }
}

void TraceCodec::processOnlineNonReferenceEdge(
  OnlineTrajectoryState& state,
  int edge
) {
  if (state.hasActiveFactor) {
    std::unordered_map<int, size_t>::const_iterator found =
      refIdToIndex_.find(state.activeRefId);
    const TraceReference* reference = findReferenceSnapshot(state.activeRefId);
    if (reference != NULL) {
      const int next = state.activeOffset + state.activeLength;
      if (next < (int)reference->edges.size() &&
          reference->edges.at((size_t)next) == edge) {
        ++state.activeLength;
        std::unordered_map<int, size_t>::iterator live =
          refIdToIndex_.find(state.activeRefId);
        if (live != refIdToIndex_.end()) {
          touchReference(references_.at(live->second));
        }
        return;
      }
    }
    recordOnlineMismatch(state, edge);
    return;
  }

  state.pending.push_back(edge);
  while ((int)state.pending.size() >= options_.kMerLength) {
    int refId = -1;
    int offset = 0;
    if (findInitialKMerMatch(state.pending, refId, offset)) {
      confirmPendingRewrite(state, refId, offset);
      state.hasActiveFactor = true;
      state.activeRefId = refId;
      state.activeOffset = offset;
      state.activeLength = options_.kMerLength;
      std::unordered_map<int, size_t>::iterator found = refIdToIndex_.find(refId);
      if (found != refIdToIndex_.end()) {
        touchReference(references_.at(found->second));
      }
      state.pending.erase(
        state.pending.begin(),
        state.pending.begin() + options_.kMerLength
      );
      return;
    }
    state.hasPendingRewrite = false;
    state.mode = OnlineTrajectoryState::REFERENCE;
    state.referenceId = addReference(state.pending, 1.0);
    for (size_t i = 0; i < state.pending.size(); ++i) {
      state.output.spatialSegments.push_back(TraceSegment::literal(state.pending.at(i)));
    }
    state.output.literalEdgeCount += state.pending.size();
    runtimeStats_.literalOutputs += state.pending.size();
    state.pending.clear();
    return;
  }
}

namespace {

bool shouldRewriteAt(
  const TraceReference& reference,
  int position,
  int mismatch,
  int k,
  double alpha
) {
  if (reference.hasBeenRewritten || position <= 0 ||
      position >= (int)reference.edges.size() - 1 ||
      reference.blocks.empty()) {
    return false;
  }
  const std::unordered_map<uint64_t, uint32_t>& rp =
    reference.rewriteAssociations.at((size_t)position);
  const uint64_t key = singleValueKey(mismatch);
  std::unordered_map<uint64_t, uint32_t>::const_iterator current = rp.find(key);
  if (current == rp.end() || current->second < (uint32_t)std::ceil(alpha)) {
    return false;
  }
  uint32_t maximum = 0;
  for (std::unordered_map<uint64_t, uint32_t>::const_iterator it = rp.begin();
       it != rp.end(); ++it) {
    maximum = std::max(maximum, it->second);
  }
  if (current->second < maximum) {
    return false;
  }
  const TraceBlockStats& matrix = reference.blocks.front();
  const int b = matrix.length;
  const int block = position / k;
  int intersections = 0;
  for (int x = 0; x <= block && x < b; ++x) {
    for (int y = block; y < b; ++y) {
      intersections += (int)matrix.flowAccess.at(fvIndex(b, x, y));
    }
  }
  return intersections < (int)current->second;
}

}  // namespace

void TraceCodec::compressSpatial(
  const std::vector<int>& spatial,
  std::vector<TraceSegment>& segments,
  size_t& matchedEdgeCount,
  size_t& literalEdgeCount,
  std::vector<std::vector<int> >& literalCandidates
) {
  segments.clear();
  literalCandidates.clear();
  matchedEdgeCount = 0;
  literalEdgeCount = 0;
  if (spatial.empty()) {
    return;
  }

  bool isReference = (int)spatial.size() < options_.kMerLength;
  std::vector<RefOccurrence> firstOccurrences;
  if (!isReference) {
    ++runtimeStats_.kmerLookups;
    collectKMerOccurrences(buildKMerKey(spatial, 0), firstOccurrences);
    bool exactFirst = false;
    for (size_t i = 0; i < firstOccurrences.size(); ++i) {
      std::unordered_map<int, size_t>::const_iterator ref =
        refIdToIndex_.find(firstOccurrences.at(i).refId);
      if (ref == refIdToIndex_.end()) {
        continue;
      }
      const TraceReference& reference = references_.at(ref->second);
      bool equal = true;
      for (int j = 0; j < options_.kMerLength; ++j) {
        ++runtimeStats_.edgeComparisons;
        if (reference.edges.at((size_t)(firstOccurrences.at(i).offset + j)) !=
            spatial.at((size_t)j)) {
          equal = false;
          break;
        }
      }
      if (equal) {
        exactFirst = true;
        break;
      }
    }
    isReference = !exactFirst;
  }

  if (isReference) {
    for (size_t i = 0; i < spatial.size(); ++i) {
      segments.push_back(TraceSegment::literal(spatial.at(i)));
    }
    literalEdgeCount = spatial.size();
    runtimeStats_.literalOutputs += spatial.size();
    if ((int)spatial.size() >= options_.kMerLength) {
      addReference(spatial, 1.0);
    }
    return;
  }

  size_t cursor = 0;
  while (cursor < spatial.size()) {
    if (cursor + (size_t)options_.kMerLength > spatial.size()) {
      segments.push_back(TraceSegment::literal(spatial.at(cursor++)));
      ++literalEdgeCount;
      ++runtimeStats_.literalOutputs;
      continue;
    }

    ++runtimeStats_.kmerLookups;
    std::vector<RefOccurrence> occurrences;
    collectKMerOccurrences(buildKMerKey(spatial, cursor), occurrences);
    int bestRef = -1;
    int bestOffset = 0;
    int bestLength = 0;
    for (size_t i = 0; i < occurrences.size(); ++i) {
      ++runtimeStats_.refCandidatesVisited;
      std::unordered_map<int, size_t>::const_iterator found =
        refIdToIndex_.find(occurrences.at(i).refId);
      if (found == refIdToIndex_.end()) {
        continue;
      }
      const TraceReference& reference = references_.at(found->second);
      int length = 0;
      while (cursor + (size_t)length < spatial.size() &&
             occurrences.at(i).offset + length < (int)reference.edges.size()) {
        ++runtimeStats_.edgeComparisons;
        if (spatial.at(cursor + (size_t)length) !=
            reference.edges.at((size_t)(occurrences.at(i).offset + length))) {
          break;
        }
        ++length;
      }
      if (length >= options_.kMerLength && length > bestLength) {
        bestRef = occurrences.at(i).refId;
        bestOffset = occurrences.at(i).offset;
        bestLength = length;
      }
    }

    if (bestRef < 0) {
      segments.push_back(TraceSegment::literal(spatial.at(cursor++)));
      ++literalEdgeCount;
      ++runtimeStats_.literalOutputs;
      continue;
    }

    TraceSegment factor = TraceSegment::reference(bestRef, bestOffset, bestLength);
    const TraceReference* snapshot = findReferenceSnapshot(bestRef);
    if (snapshot != NULL && snapshot->terminated) {
      factor.referenceTerminated = true;
      factor.referenceLength = snapshot->edges.size();
    }
    segments.push_back(factor);
    matchedEdgeCount += (size_t)bestLength;
    updateReferenceUsage(bestRef, bestOffset, bestLength);
    cursor += (size_t)bestLength;
    if (cursor >= spatial.size()) {
      continue;
    }

    const int mismatch = spatial.at(cursor++);
    segments.push_back(TraceSegment::literal(mismatch));
    ++literalEdgeCount;
    ++runtimeStats_.literalOutputs;

    std::unordered_map<int, size_t>::iterator found = refIdToIndex_.find(bestRef);
    const int rewritePosition = bestOffset + bestLength;
    if (options_.enableReferenceRewrite && found != refIdToIndex_.end() &&
        rewritePosition > 0 &&
        rewritePosition < (int)references_.at(found->second).edges.size() - 1) {
      TraceReference& reference = references_.at(found->second);
      ++reference.rewriteAssociations.at((size_t)rewritePosition)[singleValueKey(mismatch)];
      ++runtimeStats_.rewriteCandidatesEvaluated;
      if (shouldRewriteAt(
            reference,
            rewritePosition,
            mismatch,
            options_.kMerLength,
            options_.rewriteAlpha
          )) {
        rewriteReference(found->second, rewritePosition, mismatch);
      }
    }
  }
}

int TraceCodec::addReference(const std::vector<int>& spatial, double freshnessBoost) {
  (void)freshnessBoost;
  if ((int)spatial.size() < options_.kMerLength) {
    return -1;
  }
  TraceReference reference;
  reference.refId = nextRefId_++;
  reference.edges = spatial;
  reference.freshness = 1.0;
  reference.useCount = 1;
  reference.lastTouchedTick = currentTick_;
  references_.push_back(reference);
  registerReferenceFreshness(references_.back());
  ++runtimeStats_.referencesAdded;
  refIdToIndex_[reference.refId] = references_.size() - 1U;
  ensureReferenceMetadata(references_.back());
  for (size_t offset = 0;
       offset + (size_t)options_.kMerLength <= reference.edges.size();
       ++offset) {
    insertReferenceKMer(reference.refId, (int)offset, buildKMerKey(reference.edges, offset));
  }
  runtimeStats_.maxReferenceCount = std::max(
    runtimeStats_.maxReferenceCount,
    references_.size()
  );
  runtimeStats_.maxReferenceEdges = std::max(
    runtimeStats_.maxReferenceEdges,
    totalReferenceEdges(references_)
  );
  return reference.refId;
}

void TraceCodec::enforceReferenceDeletion() {
  if (references_.size() <= 1) {
    return;
  }
  bool deleted = false;
  while (references_.size() > 1 && !referenceFreshnessOrder_.empty()) {
    const int oldestId = referenceFreshnessOrder_.front();
    std::unordered_map<int, size_t>::iterator oldest = refIdToIndex_.find(oldestId);
    if (oldest == refIdToIndex_.end()) {
      throw std::runtime_error("TRACE freshness order references missing data.");
    }
    TraceReference& reference = references_.at(oldest->second);
    const double freshness = freshnessOf(reference);
    const double threshold = options_.deletionThreshold * freshnessSum_ /
      (double)references_.size();
    if (freshness >= threshold) {
      break;
    }
    freshnessSum_ = std::max(0.0, freshnessSum_ - freshness);
    removeReferenceKMers(oldestId);
    freshnessOrderByRef_.erase(oldestId);
    referenceFreshnessOrder_.pop_front();
    const size_t removedIndex = oldest->second;
    const size_t lastIndex = references_.size() - 1U;
    if (removedIndex != lastIndex) {
      references_.at(removedIndex) = references_.back();
    }
    references_.pop_back();
    ++runtimeStats_.referencesDeleted;
    deleted = true;
    refIdToIndex_.erase(oldest);
    if (removedIndex < references_.size()) {
      refIdToIndex_[references_.at(removedIndex).refId] = removedIndex;
    }
  }
  if (deleted) {
    return;
  }
  refIdToIndex_.clear();
  for (size_t i = 0; i < references_.size(); ++i) {
    refIdToIndex_[references_.at(i).refId] = i;
  }
}

int TraceCodec::beginOnlineTrajectory() {
  const int trajectoryId = nextOnlineTrajectoryId_++;
  onlineTrajectories_[trajectoryId] = OnlineTrajectoryState();
  ++runtimeStats_.trajectoriesCompressed;
  return trajectoryId;
}

void TraceCodec::pushOnlineEdges(
  int trajectoryId,
  const std::vector<int>& arrivedEdges,
  double timestamp
) {
  std::unordered_map<int, OnlineTrajectoryState>::iterator found =
    onlineTrajectories_.find(trajectoryId);
  if (found == onlineTrajectories_.end()) {
    throw std::runtime_error("TRACE online trajectory does not exist.");
  }
  advanceFreshness(timestamp);
  OnlineTrajectoryState& state = found->second;
  for (size_t i = 0; i < arrivedEdges.size(); ++i) {
    const int edge = arrivedEdges.at(i);
    ++state.output.originalSpatialLength;
    if (state.mode == OnlineTrajectoryState::REFERENCE) {
      state.output.spatialSegments.push_back(TraceSegment::literal(edge));
      ++state.output.literalEdgeCount;
      ++runtimeStats_.literalOutputs;
      appendReferenceEdge(state.referenceId, edge);
      continue;
    }
    if (state.mode == OnlineTrajectoryState::NON_REFERENCE) {
      processOnlineNonReferenceEdge(state, edge);
      continue;
    }

    state.pending.push_back(edge);
    if ((int)state.pending.size() < options_.kMerLength) {
      continue;
    }
    int refId = -1;
    int offset = 0;
    if (findInitialKMerMatch(state.pending, refId, offset)) {
      state.mode = OnlineTrajectoryState::NON_REFERENCE;
      state.hasActiveFactor = true;
      state.activeRefId = refId;
      state.activeOffset = offset;
      state.activeLength = options_.kMerLength;
      std::unordered_map<int, size_t>::iterator reference = refIdToIndex_.find(refId);
      if (reference != refIdToIndex_.end()) {
        touchReference(references_.at(reference->second));
      }
      state.pending.clear();
    } else {
      state.mode = OnlineTrajectoryState::REFERENCE;
      state.referenceId = addReference(state.pending, 1.0);
      for (size_t j = 0; j < state.pending.size(); ++j) {
        state.output.spatialSegments.push_back(TraceSegment::literal(state.pending.at(j)));
      }
      state.output.literalEdgeCount += state.pending.size();
      runtimeStats_.literalOutputs += state.pending.size();
      state.pending.clear();
    }
  }
  enforceReferenceDeletion();
}

void TraceCodec::finishOnlineTrajectory(
  int trajectoryId,
  TraceCompressedTrajectory& output
) {
  std::unordered_map<int, OnlineTrajectoryState>::iterator found =
    onlineTrajectories_.find(trajectoryId);
  if (found == onlineTrajectories_.end()) {
    throw std::runtime_error("TRACE online trajectory does not exist.");
  }
  OnlineTrajectoryState& state = found->second;
  if (state.mode == OnlineTrajectoryState::REFERENCE && state.referenceId >= 0) {
    std::unordered_map<int, size_t>::iterator reference =
      refIdToIndex_.find(state.referenceId);
    if (reference != refIdToIndex_.end()) {
      references_.at(reference->second).terminated = true;
    }
  }
  if (state.mode == OnlineTrajectoryState::NON_REFERENCE) {
    finalizeOnlineFactor(state);
  }
  for (size_t i = 0; i < state.pending.size(); ++i) {
    state.output.spatialSegments.push_back(TraceSegment::literal(state.pending.at(i)));
    ++state.output.literalEdgeCount;
    ++runtimeStats_.literalOutputs;
  }
  state.pending.clear();
  state.hasPendingRewrite = false;
  output = state.output;
  onlineTrajectories_.erase(found);
  enforceReferenceDeletion();
}

void TraceCodec::compressTrajectory(
  const PRESSTrajectory& input,
  TraceCompressedTrajectory& output
) {
  const std::vector<TemporalPair>& temporal = input.getTemporalComponent();
  const double timestamp = !temporal.empty() ? temporal.front().t : currentTick_ + 1.0;
  const int trajectoryId = beginOnlineTrajectory();
  pushOnlineEdges(trajectoryId, input.getSpatialComponent(), timestamp);
  finishOnlineTrajectory(trajectoryId, output);
  output.temporalComponent = temporal;
}

void TraceCodec::decompressSpatial(
  const std::vector<TraceSegment>& segments,
  std::vector<int>& spatial
) const {
  spatial.clear();
  for (size_t i = 0; i < segments.size(); ++i) {
    const TraceSegment& segment = segments.at(i);
    if (!segment.isReference) {
      spatial.push_back(segment.literalEdge);
      continue;
    }
    const TraceReference* reference = findReferenceSnapshot(segment.refId);
    if (reference == NULL) {
      throw std::runtime_error("TRACE decompression failed: missing reference.");
    }
    if (segment.offset < 0 || segment.length <= 0 ||
        segment.offset + segment.length > (int)reference->edges.size()) {
      throw std::runtime_error("TRACE decompression failed: invalid reference span.");
    }
    spatial.insert(
      spatial.end(),
      reference->edges.begin() + segment.offset,
      reference->edges.begin() + segment.offset + segment.length
    );
  }
}

void TraceCodec::decompressTrajectory(
  const TraceCompressedTrajectory& input,
  PRESSTrajectory& output
) const {
  std::vector<int> spatial;
  decompressSpatial(input.spatialSegments, spatial);
  output.setPRESSTrajectory(spatial, input.temporalComponent);
}

const std::vector<TraceReference>& TraceCodec::getReferences() const {
  return references_;
}

const TraceReference* TraceCodec::findReferenceForDecoding(int refId) const {
  std::unordered_map<int, size_t>::const_iterator live = refIdToIndex_.find(refId);
  if (live != refIdToIndex_.end()) {
    return &references_.at(live->second);
  }
  return NULL;
}

size_t TraceCodec::getTotalReferenceEdges() const {
  return totalReferenceEdges(references_);
}

const TraceRuntimeStats& TraceCodec::getRuntimeStats() const {
  return runtimeStats_;
}

size_t TraceCodec::getKMerIndexSize() const {
  size_t count = 0;
  for (size_t i = 0; i < kmerBucketHeads_.size(); ++i) {
    if (kmerBucketHeads_.at(i) >= 0) {
      ++count;
    }
  }
  return count;
}

size_t TraceCodec::getKMerOccurrenceCount() const {
  return activeKMerEntries_;
}

size_t TraceCodec::getPatternFrequencyCount() const {
  return 0;
}

size_t TraceCodec::getReferenceBlockCellCount() const {
  size_t count = 0;
  for (size_t i = 0; i < references_.size(); ++i) {
    if (!references_.at(i).blocks.empty()) {
      count += references_.at(i).blocks.front().flowAccess.size();
    }
  }
  return count;
}
