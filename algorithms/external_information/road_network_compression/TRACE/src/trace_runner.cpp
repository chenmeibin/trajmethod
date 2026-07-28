#include <cerrno>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "trace_bitcodec.hpp"
#include "trace_codec.hpp"
#include "trace_metrics.hpp"
#include "online_map_matcher.hpp"
#include "utcq.hpp"

#include "../vendor_press/io/file_reader.hpp"
#include "../vendor_press/io/file_writer.hpp"
#include "../vendor_press/service/trajectory_reformattor.hpp"
#include "../vendor_press/topology/grid_index.hpp"
#include "../vendor_press/topology/sp_table.hpp"
#include "../vendor_press/user_defined/factory.hpp"
#include "../vendor_press/util/helper.hpp"

namespace {

struct RunnerOptions {
  std::string root;
  std::string dataset;
  std::string summaryPath;
  std::string outputDir;
  std::string bitstreamPath;
  std::string gpsListFile;
  std::string roadNetworkPath;
  std::string preprocessingTag;
  std::vector<std::string> gpsFiles;
  bool useExistingBinaries;
  double maxSpDist;
  unsigned int encodingGamma;
  double speedErrorEta;
  int kMerLength;
  double freshnessDecay;
  double deletionThreshold;
  double rewriteAlpha;

  RunnerOptions()
    : root("."),
      dataset("T-drive"),
      useExistingBinaries(true),
      maxSpDist(4000.0),
      encodingGamma(3),
      speedErrorEta(3.0),
      kMerLength(7),
      freshnessDecay(0.998),
      deletionThreshold(0.5),
      rewriteAlpha(2.0) { }
};

std::string ensureTrailingSlash(const std::string& path) {
  if (!path.empty() && (path.at(path.size() - 1) == '/' || path.at(path.size() - 1) == '\\')) {
    return path;
  }
  return path + "/";
}

bool fileExists(const std::string& path) {
  std::ifstream input(path.c_str(), std::ios::binary);
  return input.good();
}

std::string joinPath(const std::string& left, const std::string& right) {
  return ensureTrailingSlash(left) + right;
}

template <typename T>
void writeExact(std::ofstream& output, const T& value) {
  output.write(reinterpret_cast<const char*>(&value), sizeof(T));
  if (!output.good()) {
    throw std::runtime_error("TRACE preprocessing writer failed.");
  }
}

template <typename T>
void readExact(std::ifstream& input, T& value) {
  input.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (!input.good()) {
    throw std::runtime_error("TRACE preprocessing reader failed.");
  }
}

void storeTraceGraphBinary(const std::string& path, const Graph& graph) {
  std::ofstream output(path.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
  if (!output.good()) {
    throw std::runtime_error("TRACE failed to open trace graph binary for writing.");
  }

  const char magic[8] = {'T', 'R', 'A', 'C', 'E', 'G', '1', '\0'};
  output.write(magic, sizeof(magic));

  uint64_t nodeCount = (uint64_t)graph.getNodeNumber();
  writeExact(output, nodeCount);
  for (size_t nodeIndex = 0; nodeIndex < graph.getNodeNumber(); ++nodeIndex) {
    const Node& node = graph.getNode(nodeIndex);
    writeExact(output, node.getPosition().x);
    writeExact(output, node.getPosition().y);
    uint64_t edgeCount = (uint64_t)node.getEdgeNumber();
    writeExact(output, edgeCount);
    for (size_t edgeOffset = 0; edgeOffset < node.getEdgeNumber(); ++edgeOffset) {
      int edgeId = node.getEdgeId(edgeOffset);
      writeExact(output, edgeId);
    }
  }

  uint64_t edgeCount = (uint64_t)graph.getEdgeNumber();
  writeExact(output, edgeCount);
  for (size_t edgeIndex = 0; edgeIndex < graph.getEdgeNumber(); ++edgeIndex) {
    const Edge& edge = graph.getEdge(edgeIndex);
    int sourceId = edge.getSourceId();
    int targetId = edge.getTargetId();
    writeExact(output, sourceId);
    writeExact(output, targetId);
    uint64_t shapeSize = (uint64_t)edge.getGeoSize();
    writeExact(output, shapeSize);
    for (size_t pointIndex = 0; pointIndex < edge.getGeoSize(); ++pointIndex) {
      const Point2D& point = edge.getShape().at(pointIndex);
      writeExact(output, point.x);
      writeExact(output, point.y);
    }
  }
}

void loadTraceGraphBinary(const std::string& path, Graph& graph) {
  std::ifstream input(path.c_str(), std::ios::binary | std::ios::in);
  if (!input.good()) {
    throw std::runtime_error("TRACE failed to open trace graph binary for reading.");
  }

  char magic[8] = {0};
  input.read(magic, sizeof(magic));
  const std::string expectedMagic("TRACEG1");
  if (!input.good() || std::string(magic) != expectedMagic) {
    throw std::runtime_error("TRACE graph binary magic mismatch.");
  }

  uint64_t nodeCount = 0;
  readExact(input, nodeCount);
  std::vector<Node> nodeList;
  nodeList.reserve((size_t)nodeCount);
  for (uint64_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
    double x = 0.0;
    double y = 0.0;
    readExact(input, x);
    readExact(input, y);
    nodeList.emplace_back(Node(x, y));
    uint64_t edgeCount = 0;
    readExact(input, edgeCount);
    for (uint64_t edgeOffset = 0; edgeOffset < edgeCount; ++edgeOffset) {
      int edgeId = EDGE_NOT_EXIST;
      readExact(input, edgeId);
      nodeList.back().addEdge(edgeId);
    }
  }

  uint64_t edgeCount = 0;
  readExact(input, edgeCount);
  std::vector<Edge> edgeList;
  edgeList.reserve((size_t)edgeCount);
  for (uint64_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
    int sourceId = 0;
    int targetId = 0;
    readExact(input, sourceId);
    readExact(input, targetId);
    edgeList.emplace_back(Edge(sourceId, targetId));
    uint64_t shapeSize = 0;
    readExact(input, shapeSize);
    for (uint64_t pointIndex = 0; pointIndex < shapeSize; ++pointIndex) {
      double x = 0.0;
      double y = 0.0;
      readExact(input, x);
      readExact(input, y);
      edgeList.back().addPosition(x, y);
    }
  }

  graph.setGraph(nodeList, edgeList);
}

std::string defaultGpsReaderTypeForDataset(const std::string& dataset) {
  if (dataset == "T-drive" || dataset == "tdrive") {
    return "TDRIVE_GPS";
  }
  if (dataset == "chengdu" || dataset == "Chengdu") {
    return "CHENGDU_GPS";
  }
  if (dataset == "Geolife" || dataset == "geolife") {
    return "GEOLIFE_GPS";
  }
  throw std::runtime_error("Unsupported dataset for TRACE runner.");
}

std::string normalizeGpsFile(const std::string& dataset, const std::string& gpsFile) {
  if (gpsFile.find('/') != std::string::npos || gpsFile.find('\\') != std::string::npos) {
    return gpsFile;
  }
  return dataset + "/" + gpsFile;
}

struct TimedGpsInput {
  std::string file;
  double firstTimestamp;
  size_t originalOrder;
};

double readFirstGpsTimestamp(const std::string& path) {
  std::ifstream input(path.c_str());
  std::string line;
  while (std::getline(input, line)) {
    for (size_t offset = 0; offset + 18 < line.size(); ++offset) {
      if (line.at(offset) != '2' || line.at(offset + 4) != '-' ||
          line.at(offset + 7) != '-') {
        continue;
      }
      int year = 0;
      int month = 0;
      int day = 0;
      int hour = 0;
      int minute = 0;
      int second = 0;
      if (std::sscanf(
            line.c_str() + offset,
            "%d-%d-%d %d:%d:%d",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second
          ) == 6) {
        std::tm timeInfo = {};
        timeInfo.tm_year = year - 1900;
        timeInfo.tm_mon = month - 1;
        timeInfo.tm_mday = day;
        timeInfo.tm_hour = hour;
        timeInfo.tm_min = minute;
        timeInfo.tm_sec = second;
        return (double)std::mktime(&timeInfo);
      }
    }
  }
  return std::numeric_limits<double>::max();
}

void orderInputsByFirstTimestamp(RunnerOptions& options) {
  std::vector<TimedGpsInput> timed;
  timed.reserve(options.gpsFiles.size());
  const std::string dataRoot = joinPath(options.root, "data");
  for (size_t i = 0; i < options.gpsFiles.size(); ++i) {
    TimedGpsInput input;
    input.file = options.gpsFiles.at(i);
    input.firstTimestamp = readFirstGpsTimestamp(joinPath(dataRoot, input.file));
    input.originalOrder = i;
    timed.push_back(input);
  }
  std::stable_sort(
    timed.begin(),
    timed.end(),
    [](const TimedGpsInput& left, const TimedGpsInput& right) {
      if (left.firstTimestamp != right.firstTimestamp) {
        return left.firstTimestamp < right.firstTimestamp;
      }
      return left.originalOrder < right.originalOrder;
    }
  );
  for (size_t i = 0; i < timed.size(); ++i) {
    options.gpsFiles.at(i) = timed.at(i).file;
  }
}

RunnerOptions parseArgs(int argc, char** argv) {
  RunnerOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      options.root = argv[++i];
    } else if (arg == "--dataset" && i + 1 < argc) {
      options.dataset = argv[++i];
    } else if (arg == "--gps-file" && i + 1 < argc) {
      options.gpsFiles.push_back(argv[++i]);
    } else if (arg == "--gps-list-file" && i + 1 < argc) {
      options.gpsListFile = argv[++i];
    } else if (arg == "--road-network" && i + 1 < argc) {
      options.roadNetworkPath = argv[++i];
    } else if (arg == "--preprocessing-tag" && i + 1 < argc) {
      options.preprocessingTag = argv[++i];
    } else if (arg == "--summary" && i + 1 < argc) {
      options.summaryPath = argv[++i];
    } else if (arg == "--output-dir" && i + 1 < argc) {
      options.outputDir = argv[++i];
    } else if (arg == "--bitstream" && i + 1 < argc) {
      options.bitstreamPath = argv[++i];
    } else if (arg == "--use-existing-binaries") {
      options.useExistingBinaries = true;
    } else if (arg == "--build-preprocessing") {
      options.useExistingBinaries = false;
    } else if (arg == "--k" && i + 1 < argc) {
      options.kMerLength = std::atoi(argv[++i]);
    } else if (arg == "--deletion-threshold" && i + 1 < argc) {
      options.deletionThreshold = std::atof(argv[++i]);
    } else if (arg == "--rewrite-alpha" && i + 1 < argc) {
      options.rewriteAlpha = std::atof(argv[++i]);
    } else if (arg == "--freshness-decay" && i + 1 < argc) {
      options.freshnessDecay = std::atof(argv[++i]);
    } else if (arg == "--max-sp-dist" && i + 1 < argc) {
      options.maxSpDist = std::atof(argv[++i]);
    } else if (arg == "--gamma" && i + 1 < argc) {
      options.encodingGamma = (unsigned int)std::strtoul(argv[++i], NULL, 10);
    } else if (arg == "--eta" && i + 1 < argc) {
      options.speedErrorEta = std::atof(argv[++i]);
    } else {
      throw std::runtime_error("Unknown or incomplete TRACE runner argument: " + arg);
    }
  }

  if (options.gpsFiles.empty() && !options.gpsListFile.empty()) {
    std::ifstream listInput(options.gpsListFile.c_str());
    if (!listInput.good()) {
      throw std::runtime_error("TRACE runner failed to open --gps-list-file.");
    }
    std::string gpsFile;
    while (std::getline(listInput, gpsFile)) {
      while (!gpsFile.empty() &&
             (gpsFile.at(gpsFile.size() - 1) == '\r' ||
              gpsFile.at(gpsFile.size() - 1) == ' ' ||
              gpsFile.at(gpsFile.size() - 1) == '\t')) {
        gpsFile.erase(gpsFile.size() - 1);
      }
      if (!gpsFile.empty()) {
        options.gpsFiles.push_back(gpsFile);
      }
    }
  }

  if (options.gpsFiles.empty()) {
    throw std::runtime_error("TRACE runner requires at least one --gps-file.");
  }
  for (size_t i = 0; i < options.gpsFiles.size(); ++i) {
    options.gpsFiles.at(i) = normalizeGpsFile(options.dataset, options.gpsFiles.at(i));
  }
  return options;
}

void loadOrBuildPreprocessing(
  const RunnerOptions& options,
  Graph& graph,
  GridIndex& gridIndex,
  SPTable& spTable
) {
  const std::string dataRoot = joinPath(options.root, "data");
  const std::string expRoot = joinPath(options.root, "experiments");
  const std::string datasetRoot = options.dataset;
  const std::string roadNetworkText = options.roadNetworkPath.empty()
    ? joinPath(joinPath(dataRoot, datasetRoot), "road_network.txt")
    : options.roadNetworkPath;
  const std::string tracePrefix = options.preprocessingTag.empty()
    ? "trace_" : "trace_" + options.preprocessingTag + "_";
  const std::string pressRoadNetworkBin = joinPath(joinPath(expRoot, datasetRoot), "road_network.bin");
  const std::string roadNetworkBin = joinPath(
    joinPath(expRoot, datasetRoot), tracePrefix + "road_network.bin"
  );
  const std::string gridIndexBin = joinPath(
    joinPath(expRoot, datasetRoot), tracePrefix + "grid_index.bin"
  );
  const std::string spTableBin = joinPath(
    joinPath(expRoot, datasetRoot), tracePrefix + "sp_table.bin"
  );
  const std::string legacyGridIndexBin = joinPath(joinPath(expRoot, datasetRoot), "grid_index.bin");
  const std::string legacySpTableBin = joinPath(joinPath(expRoot, datasetRoot), "sp_table.bin");
  const bool hasTracePreprocessingSet =
    fileExists(roadNetworkBin) && fileExists(gridIndexBin) && fileExists(spTableBin);
  const bool mayReusePressPreprocessing =
    options.roadNetworkPath.empty() && options.preprocessingTag.empty();
  const bool hasPressPreprocessingSet = mayReusePressPreprocessing &&
    fileExists(pressRoadNetworkBin) &&
    fileExists(legacyGridIndexBin) &&
    fileExists(legacySpTableBin);

  if (options.useExistingBinaries && (hasTracePreprocessingSet || hasPressPreprocessingSet)) {
    const std::string graphPath = hasTracePreprocessingSet
      ? roadNetworkBin : pressRoadNetworkBin;
    FileReader graphReader(graphPath.c_str(), true);
    graph.load(graphReader);
  } else {
    std::shared_ptr<GraphReaderBase> graphReader = Factory::getGraphReader(GraphReaderType::PRESS_ROADNET);
    graphReader->readGraph(roadNetworkText, graph);
    FileWriter graphWriter(roadNetworkBin.c_str(), true);
    graph.store(graphWriter);
  }

  const std::string reusableGridIndex = hasTracePreprocessingSet
    ? gridIndexBin : legacyGridIndexBin;
  if (options.useExistingBinaries && fileExists(reusableGridIndex)) {
    FileReader gridReader(reusableGridIndex.c_str(), true);
    gridIndex.load(gridReader);
  } else {
    gridIndex.build(graph, 50, 50);
    FileWriter gridWriter(gridIndexBin.c_str(), true);
    gridIndex.store(gridWriter);
  }

  const std::string reusableSpTable = hasTracePreprocessingSet
    ? spTableBin : legacySpTableBin;
  if (options.useExistingBinaries && fileExists(reusableSpTable)) {
    FileReader spReader(reusableSpTable.c_str(), true);
    spTable.load(spReader);
  } else {
    spTable.build(graph, options.maxSpDist);
    FileWriter spWriter(spTableBin.c_str(), true);
    spTable.store(spWriter);
  }
}


size_t compressedSpatialTokenCount(const TraceCompressedTrajectory& compressed) {
  size_t tokenCount = 0;
  for (size_t i = 0; i < compressed.spatialSegments.size(); ++i) {
    tokenCount += compressed.spatialSegments.at(i).isReference ? 3U : 1U;
  }
  return tokenCount;
}

void applySpeedFactors(
  const TraceCompressedTrajectory& compressedSpeeds,
  TraceUtcqRepresentation& temporal
) {
  temporal.speedFactors.clear();
  size_t position = 0;
  for (size_t i = 0; i < compressedSpeeds.spatialSegments.size(); ++i) {
    const TraceSegment& segment = compressedSpeeds.spatialSegments.at(i);
    TraceSpeedFactor factor;
    factor.isReference = segment.isReference;
    if (segment.isReference) {
      factor.refId = segment.refId;
      factor.offset = segment.offset;
      factor.length = segment.length;
      factor.referenceTerminated = segment.referenceTerminated;
      factor.referenceLength = segment.referenceLength;
      position += (size_t)segment.length;
    } else {
      if (position >= temporal.growthRateCodes.size()) {
        throw std::runtime_error("TRACE speed factor position overflow.");
      }
      factor.mismatchGrowthRateCode = temporal.growthRateCodes.at(position++);
    }
    temporal.speedFactors.push_back(factor);
  }
  if (position != temporal.growthRateCodes.size()) {
    throw std::runtime_error("TRACE speed factors do not cover all speeds.");
  }
}

void resolveSpeedReferences(
  const TraceCodec& speedCodec,
  double eta,
  UtcqCodec& utcqCodec,
  TraceUtcqRepresentation& temporal
) {
  std::vector<double> speeds(temporal.growthRateCodes.size(), 0.0);
  std::vector<bool> usesReference(temporal.growthRateCodes.size(), false);
  size_t position = 0;
  for (size_t i = 0; i < temporal.speedFactors.size(); ++i) {
    const TraceSpeedFactor& factor = temporal.speedFactors.at(i);
    if (!factor.isReference) {
      ++position;
      continue;
    }
    const TraceReference* reference = speedCodec.findReferenceForDecoding(factor.refId);
    if (reference == NULL || factor.offset < 0 || factor.length <= 0 ||
        (size_t)(factor.offset + factor.length) > reference->edges.size() ||
        position + (size_t)factor.length > speeds.size()) {
      throw std::runtime_error("TRACE cannot resolve a speed reference factor.");
    }
    for (int j = 0; j < factor.length; ++j) {
      const int speedCode = reference->edges.at((size_t)(factor.offset + j));
      speeds.at(position) = eta > 0.0
        ? (double)speedCode * 0.5 * eta
        : (double)speedCode / 1000.0;
      usesReference.at(position) = true;
      ++position;
    }
  }
  if (position != speeds.size()) {
    throw std::runtime_error("TRACE resolved speed length mismatch.");
  }
  utcqCodec.reconstructTemporal(temporal, speeds, usesReference);
}

std::vector<std::vector<int> > buildEdgeArrivalChunks(
  const Graph& graph,
  const std::vector<int>& edgePath,
  const std::vector<TemporalPair>& temporal
) {
  std::vector<std::vector<int> > chunks(temporal.size());
  if (edgePath.empty() || temporal.empty()) {
    return chunks;
  }
  size_t currentEdge = 0;
  double edgeEndDistance = graph.getEdge((size_t)edgePath.front()).getDistance();
  size_t previousEdge = (size_t)-1;
  for (size_t i = 0; i < temporal.size(); ++i) {
    while (currentEdge + 1 < edgePath.size() &&
           temporal.at(i).dist > edgeEndDistance) {
      ++currentEdge;
      edgeEndDistance += graph.getEdge((size_t)edgePath.at(currentEdge)).getDistance();
    }
    const size_t begin = previousEdge == (size_t)-1 ? 0 : previousEdge + 1;
    if (begin <= currentEdge) {
      chunks.at(i).insert(
        chunks.at(i).end(),
        edgePath.begin() + (std::vector<int>::difference_type)begin,
        edgePath.begin() + (std::vector<int>::difference_type)(currentEdge + 1)
      );
    }
    previousEdge = currentEdge;
  }
  if (previousEdge + 1 < edgePath.size()) {
    chunks.back().insert(
      chunks.back().end(),
      edgePath.begin() + (std::vector<int>::difference_type)(previousEdge + 1),
      edgePath.end()
    );
  }
  return chunks;
}

struct PreparedTraceSegment {
  size_t fileIndex;
  size_t segmentIndex;
  size_t sourceGpsLength;
  std::string gpsFile;
  GPSTrajectory gpsSegment;
  PRESSTrajectory traceInput;
  int startNodeId;
  std::vector<int> encodedSpatial;
  std::vector<unsigned int> encodedSpatialWidths;
  TraceUtcqRepresentation originalTemporal;
  std::vector<std::vector<int> > encodedArrivalChunks;
  int spatialTrajectoryId;
  int speedTrajectoryId;
  size_t arrivalOrderBase;

  PreparedTraceSegment()
    : fileIndex(0),
      segmentIndex(0),
      sourceGpsLength(0),
      startNodeId(-1),
      spatialTrajectoryId(-1),
      speedTrajectoryId(-1),
      arrivalOrderBase(0) { }
};

struct TraceArrivalEvent {
  double timestamp;
  size_t preparedIndex;
  size_t arrivalIndex;
  size_t stableOrder;
};

struct LaterTraceArrival {
  bool operator()(const TraceArrivalEvent& left, const TraceArrivalEvent& right) const {
    if (left.timestamp != right.timestamp) {
      return left.timestamp > right.timestamp;
    }
    return left.stableOrder > right.stableOrder;
  }
};

unsigned int outgoingChoiceBitWidth(size_t outgoingCount) {
  if (outgoingCount <= 1U) {
    return 0U;
  }
  unsigned int width = 0;
  for (size_t largestCode = outgoingCount - 1U; largestCode > 0; largestCode >>= 1U) {
    ++width;
  }
  return width;
}

bool buildOutgoingChoiceWidths(
  const Graph& graph,
  int startNodeId,
  const std::vector<int>& outgoingChoices,
  std::vector<unsigned int>& widths,
  std::string& error
) {
  widths.clear();
  error.clear();
  int currentNodeId = startNodeId;
  for (size_t i = 0; i < outgoingChoices.size(); ++i) {
    if (currentNodeId < 0 || currentNodeId >= (int)graph.getNodeNumber()) {
      error = "TRACE outgoing-width traversal reached an invalid node.";
      widths.clear();
      return false;
    }
    const Node& node = graph.getNode((size_t)currentNodeId);
    const int choice = outgoingChoices.at(i);
    if (choice < 0 || choice >= (int)node.getEdgeNumber()) {
      error = "TRACE outgoing-width traversal found an invalid outgoing choice.";
      widths.clear();
      return false;
    }
    widths.push_back(outgoingChoiceBitWidth(node.getEdgeNumber()));
    currentNodeId = graph.getEdge((size_t)node.getEdgeId((size_t)choice)).getTargetId();
  }
  return true;
}

bool decodeOutgoingChoiceSequence(
  const Graph& graph,
  int startNodeId,
  const std::vector<int>& outgoingChoices,
  std::vector<int>& edgePath
);

std::vector<std::vector<int> > convertArrivalChunks(
  const std::vector<std::vector<int> >& edgeChunks,
  const std::vector<int>& encodedSpatial
) {
  std::vector<std::vector<int> > encodedChunks(edgeChunks.size());
  size_t offset = 0;
  for (size_t i = 0; i < edgeChunks.size(); ++i) {
    const size_t count = edgeChunks.at(i).size();
    if (offset + count > encodedSpatial.size()) {
      throw std::runtime_error("TRACE arrival chunks exceed the spatial sequence.");
    }
    encodedChunks.at(i).insert(
      encodedChunks.at(i).end(),
      encodedSpatial.begin() + (std::vector<int>::difference_type)offset,
      encodedSpatial.begin() + (std::vector<int>::difference_type)(offset + count)
    );
    offset += count;
  }
  if (offset != encodedSpatial.size()) {
    throw std::runtime_error("TRACE arrival chunks do not cover the spatial sequence.");
  }
  return encodedChunks;
}

void flushOnlineMatchedSegment(
  std::vector<GPSPoint>& gpsPoints,
  std::vector<int>& edgeIds,
  std::vector<GPSTrajectory>& gpsSegments,
  std::vector<MapMatchedTrajectory>& matchedSegments
) {
  if (!gpsPoints.empty() && gpsPoints.size() == edgeIds.size()) {
    gpsSegments.push_back(GPSTrajectory(gpsPoints));
    matchedSegments.push_back(MapMatchedTrajectory(edgeIds));
  }
  gpsPoints.clear();
  edgeIds.clear();
}

void onlineMapMatchTrajectory(
  const Graph& graph,
  const GridIndex& gridIndex,
  const SPTable& spTable,
  const GPSTrajectory& input,
  std::vector<GPSTrajectory>& gpsSegments,
  std::vector<MapMatchedTrajectory>& matchedSegments,
  OnlineMapMatchingStats& aggregateStats
) {
  OnlineMapMatcher matcher(graph, gridIndex);
  std::vector<GPSPoint> gpsPoints;
  std::vector<int> edgeIds;
  for (size_t i = 0; i < input.getLength(); ++i) {
    const GPSPoint& gpsPoint = input.getGPSPoint(i);
    OnlineMatchedPoint matched;
    if (!matcher.push(gpsPoint, matched)) {
      flushOnlineMatchedSegment(
        gpsPoints,
        edgeIds,
        gpsSegments,
        matchedSegments
      );
      matcher.reset();
      continue;
    }

    bool connected = true;
    if (!edgeIds.empty() && edgeIds.back() != matched.edgeId) {
      const int source = graph.getEdge((size_t)edgeIds.back()).getTargetId();
      const int target = graph.getEdge((size_t)matched.edgeId).getSourceId();
      connected = source == target ||
        spTable.prevEdgeIndex((size_t)source, (size_t)target) != EDGE_NOT_EXIST;
    }
    if (!connected) {
      flushOnlineMatchedSegment(
        gpsPoints,
        edgeIds,
        gpsSegments,
        matchedSegments
      );
      matcher.reset();
      if (!matcher.push(gpsPoint, matched)) {
        continue;
      }
    }
    gpsPoints.push_back(gpsPoint);
    edgeIds.push_back(matched.edgeId);
  }
  flushOnlineMatchedSegment(
    gpsPoints,
    edgeIds,
    gpsSegments,
    matchedSegments
  );
  const OnlineMapMatchingStats& stats = matcher.getStats();
  aggregateStats.pointsProcessed += stats.pointsProcessed;
  aggregateStats.pointsMatched += stats.pointsMatched;
  aggregateStats.initializations += stats.initializations;
  aggregateStats.predictionCandidates += stats.predictionCandidates;
  aggregateStats.prunedCandidates += stats.prunedCandidates;
  aggregateStats.predictionBranches += stats.predictionBranches;
}

void finishPreparedSegment(
  const PreparedTraceSegment& prepared,
  TraceCompressedTrajectory compressed,
  TraceCompressedTrajectory compressedSpeeds,
  TraceCodec& speedCodec,
  UtcqCodec& utcqCodec,
  const TraceBitCodec& traceBitCodec,
  const RunnerOptions& options,
  TraceRunMetrics& runMetrics,
  std::vector<TraceBinaryPacket>& outputPackets,
  double compressionMillis
) {
  typedef std::chrono::steady_clock Clock;
  compressed.startNodeId = prepared.startNodeId;
  compressed.temporalComponent = prepared.traceInput.getTemporalComponent();
  TraceUtcqRepresentation compressedTemporal = prepared.originalTemporal;
  applySpeedFactors(compressedSpeeds, compressedTemporal);
  resolveSpeedReferences(
    speedCodec,
    options.speedErrorEta,
    utcqCodec,
    compressedTemporal
  );

  const TraceBinaryPacket originalPacket = traceBitCodec.encodeOriginal(
    prepared.startNodeId,
    prepared.encodedSpatial,
    prepared.originalTemporal
  );
  const TraceBinaryPacket compressedPacket = traceBitCodec.encodePaperCompressed(
    compressed,
    compressedTemporal,
    prepared.encodedSpatialWidths
  );
  outputPackets.push_back(compressedPacket);
  runMetrics.totalCompressionMillis += compressionMillis;

  ++runMetrics.totalMatchedSegments;
  runMetrics.totalOriginalEdges += prepared.encodedSpatial.size();
  runMetrics.totalMatchedEdges += compressed.matchedEdgeCount;
  runMetrics.totalLiteralEdges += compressed.literalEdgeCount;
  runMetrics.totalCompressedSpatialTokens += compressedSpatialTokenCount(compressed);
  runMetrics.totalOriginalSpeeds += compressedSpeeds.originalSpatialLength;
  runMetrics.totalMatchedSpeeds += compressedSpeeds.matchedEdgeCount;
  runMetrics.totalLiteralSpeeds += compressedSpeeds.literalEdgeCount;
  for (size_t i = 0; i < compressed.spatialSegments.size(); ++i) {
    if (compressed.spatialSegments.at(i).isReference &&
        compressed.spatialSegments.at(i).referenceTerminated) {
      ++runMetrics.terminatedSpatialFactors;
    }
  }
  for (size_t i = 0; i < compressedTemporal.speedFactors.size(); ++i) {
    if (compressedTemporal.speedFactors.at(i).isReference &&
        compressedTemporal.speedFactors.at(i).referenceTerminated) {
      ++runMetrics.terminatedSpeedFactors;
    }
  }
  runMetrics.totalOriginalSpatialBits += originalPacket.spatialBits;
  runMetrics.totalCompressedSpatialBits += compressedPacket.spatialBits;
  runMetrics.totalOriginalTemporalBits += originalPacket.temporalBits;
  runMetrics.totalCompressedTemporalBits += compressedPacket.temporalBits;
  runMetrics.totalOriginalFramingBits += originalPacket.framingBits;
  runMetrics.totalCompressedFramingBits += compressedPacket.framingBits;
  runMetrics.totalOriginalBitstreamBytes += originalPacket.bytes.size();
  runMetrics.totalCompressedBitstreamBytes += compressedPacket.bytes.size();
}


bool encodeOutgoingChoiceSequence(
  const Graph& graph,
  const std::vector<int>& edgePath,
  int& startNodeId,
  std::vector<int>& outgoingChoices,
  std::string& error
) {
  outgoingChoices.clear();
  error.clear();
  startNodeId = -1;
  if (edgePath.empty()) {
    return true;
  }

  const Edge& firstEdge = graph.getEdge((size_t)edgePath.at(0));
  startNodeId = firstEdge.getSourceId();
  int currentNodeId = startNodeId;
  for (size_t i = 0; i < edgePath.size(); ++i) {
    const Node& node = graph.getNode((size_t)currentNodeId);
    int localChoice = -1;
    for (size_t edgeIndex = 0; edgeIndex < node.getEdgeNumber(); ++edgeIndex) {
      if (node.getEdgeId(edgeIndex) == edgePath.at(i)) {
        localChoice = (int)edgeIndex;
        break;
      }
    }
    if (localChoice < 0) {
      std::ostringstream detail;
      detail
        << "edge " << edgePath.at(i)
        << " at path offset " << i
        << " is not outgoing from node " << currentNodeId;
      error = detail.str();
      outgoingChoices.clear();
      startNodeId = -1;
      return false;
    }
    outgoingChoices.push_back(localChoice);
    currentNodeId = graph.getEdge((size_t)edgePath.at(i)).getTargetId();
  }
  return true;
}

bool decodeOutgoingChoiceSequence(
  const Graph& graph,
  int startNodeId,
  const std::vector<int>& outgoingChoices,
  std::vector<int>& edgePath
) {
  edgePath.clear();
  if (outgoingChoices.empty()) {
    return true;
  }
  if (startNodeId < 0 || startNodeId >= (int)graph.getNodeNumber()) {
    return false;
  }

  int currentNodeId = startNodeId;
  for (size_t i = 0; i < outgoingChoices.size(); ++i) {
    const Node& node = graph.getNode((size_t)currentNodeId);
    int localIndex = outgoingChoices.at(i);
    if (localIndex < 0 || localIndex >= (int)node.getEdgeNumber()) {
      edgePath.clear();
      return false;
    }
    int edgeId = node.getEdgeId((size_t)localIndex);
    edgePath.push_back(edgeId);
    currentNodeId = graph.getEdge((size_t)edgeId).getTargetId();
  }
  return true;
}

std::vector<int> sanitizeFallbackEdgeSequence(const std::vector<int>& mmEdges) {
  std::vector<int> sanitized;
  sanitized.reserve(mmEdges.size());

  for (size_t i = 0; i < mmEdges.size(); ++i) {
    int edgeId = mmEdges.at(i);
    if (!sanitized.empty() && sanitized.back() == edgeId) {
      continue;
    }
    if (sanitized.size() >= 2 && sanitized.at(sanitized.size() - 2) == edgeId) {
      continue;
    }
    sanitized.push_back(edgeId);
  }

  return sanitized;
}


}  // namespace

int main(int argc, char** argv) {
  try {
    typedef std::chrono::steady_clock Clock;
    RunnerOptions options = parseArgs(argc, argv);

    Graph graph;
    GridIndex gridIndex;
    SPTable spTable;
    loadOrBuildPreprocessing(options, graph, gridIndex, spTable);

    std::shared_ptr<GPSTrajectoryReaderBase> gpsReader =
      Factory::getGPSTrajectoryReader(getGPSTrajectoryReaderType(
        defaultGpsReaderTypeForDataset(options.dataset)
      ));
    TraceCompressionOptions traceOptions;
    traceOptions.kMerLength = options.kMerLength;
    traceOptions.freshnessDecay = options.freshnessDecay;
    traceOptions.deletionThreshold = options.deletionThreshold;
    traceOptions.rewriteAlpha = options.rewriteAlpha;
    TraceCodec traceCodec(traceOptions);
    TraceCompressionOptions speedOptions = traceOptions;
    speedOptions.enableReferenceRewrite = false;
    TraceCodec speedCodec(speedOptions);
    UtcqCodec utcqCodec(UtcqTemporalParameters(options.encodingGamma, options.speedErrorEta));

    TrajectoryReformatter reformatter;
    OnlineMapMatchingStats onlineMapMatchingStats;

    unsigned int outgoingEdgeBits = 1;
    for (size_t nodeIndex = 0; nodeIndex < graph.getNodeNumber(); ++nodeIndex) {
      size_t choices = graph.getNode(nodeIndex).getEdgeNumber();
      unsigned int width = 1;
      // Definition 1 numbers outgoing edges from zero; Section 5.1 stores the
      // largest possible value in the graph-wide outgoing-edge width.
      size_t largestCode = choices == 0 ? 0 : choices - 1U;
      while (largestCode > 1) {
        largestCode >>= 1;
        ++width;
      }
      outgoingEdgeBits = std::max(outgoingEdgeBits, width);
    }
    TraceBitCodec traceBitCodec(outgoingEdgeBits);
    TraceRunMetrics runMetrics;
    runMetrics.totalGpsInputs = options.gpsFiles.size();
    std::vector<PreparedTraceSegment> preparedSegments;
    std::priority_queue<
      TraceArrivalEvent,
      std::vector<TraceArrivalEvent>,
      LaterTraceArrival
    > arrivalEvents;
    std::vector<TraceBinaryPacket> outputPackets;
    size_t stableEventOrder = 0;
    for (size_t fileIndex = 0; fileIndex < options.gpsFiles.size(); ++fileIndex) {
      GPSTrajectory gpsTrajectory;
      gpsReader->readGPSTrajectory(
        joinPath(joinPath(options.root, "data"), options.gpsFiles.at(fileIndex)),
        gpsTrajectory
      );
      runMetrics.totalGpsPoints += gpsTrajectory.getLength();
      if (gpsTrajectory.getLength() == 0) {
        ++runMetrics.skippedEmptyInputs;
        continue;
      }

      std::vector<GPSTrajectory> gpsSegments;
      std::vector<MapMatchedTrajectory> mmSegments;
      onlineMapMatchTrajectory(
        graph,
        gridIndex,
        spTable,
        gpsTrajectory,
        gpsSegments,
        mmSegments,
        onlineMapMatchingStats
      );

      for (size_t segmentIndex = 0; segmentIndex < mmSegments.size(); ++segmentIndex) {
        PRESSTrajectory pressTrajectory;
        try {
          reformatter.generateTrajectory(
            spTable,
            graph,
            gpsSegments.at(segmentIndex),
            mmSegments.at(segmentIndex),
            pressTrajectory
          );
        } catch (...) {
          ++runMetrics.skippedEmptyInputs;
          continue;
        }
        if (pressTrajectory.getSpatialLength() == 0 ||
            pressTrajectory.getTemporalLength() == 0) {
          ++runMetrics.skippedEmptyInputs;
          continue;
        }

        PreparedTraceSegment prepared;
        prepared.fileIndex = fileIndex;
        prepared.segmentIndex = segmentIndex;
        prepared.sourceGpsLength = gpsTrajectory.getLength();
        prepared.gpsFile = options.gpsFiles.at(fileIndex);
        prepared.gpsSegment = gpsSegments.at(segmentIndex);
        prepared.traceInput = pressTrajectory;
        std::string outgoingEncodingError;
        if (!encodeOutgoingChoiceSequence(
              graph,
              pressTrajectory.getSpatialComponent(),
              prepared.startNodeId,
              prepared.encodedSpatial,
              outgoingEncodingError
            )) {
          throw std::runtime_error(
            "TRACE requires a connected road-network path: " + outgoingEncodingError
          );
        }
        if (!buildOutgoingChoiceWidths(
              graph,
              prepared.startNodeId,
              prepared.encodedSpatial,
              prepared.encodedSpatialWidths,
              outgoingEncodingError
            )) {
          throw std::runtime_error(outgoingEncodingError);
        }

        double firstEdgeLength = 1.0;
        const int firstEdgeId = pressTrajectory.getSpatialComponent().front();
        if (firstEdgeId >= 0 && firstEdgeId < (int)graph.getEdgeNumber()) {
          firstEdgeLength = graph.getEdge((size_t)firstEdgeId).getDistance();
        }
        prepared.originalTemporal = utcqCodec.buildTraceRepresentation(
          pressTrajectory.getTemporalComponent(),
          firstEdgeLength,
          prepared.gpsSegment.getSampleRate()
        );
        const std::vector<std::vector<int> > edgeChunks = buildEdgeArrivalChunks(
          graph,
          pressTrajectory.getSpatialComponent(),
          pressTrajectory.getTemporalComponent()
        );
        prepared.encodedArrivalChunks = convertArrivalChunks(
          edgeChunks,
          prepared.encodedSpatial
        );
        prepared.spatialTrajectoryId = traceCodec.beginOnlineTrajectory();
        prepared.speedTrajectoryId = speedCodec.beginOnlineTrajectory();
        prepared.arrivalOrderBase = stableEventOrder;
        stableEventOrder += edgeChunks.size();

        const size_t preparedIndex = preparedSegments.size();
        preparedSegments.push_back(prepared);
        runMetrics.totalStreamArrivals += edgeChunks.size();
        for (size_t arrivalIndex = 0; arrivalIndex < edgeChunks.size(); ++arrivalIndex) {
          if (!edgeChunks.at(arrivalIndex).empty()) {
            ++runMetrics.totalNonEmptyEdgeArrivals;
          }
        }
        if (!edgeChunks.empty()) {
          TraceArrivalEvent event;
          event.timestamp = pressTrajectory.getTemporalComponent().front().t;
          event.preparedIndex = preparedIndex;
          event.arrivalIndex = 0;
          event.stableOrder = prepared.arrivalOrderBase;
          arrivalEvents.push(event);
        }
      }
    }

    runMetrics.onlineMapMatchingUpdates = onlineMapMatchingStats.pointsProcessed;
    runMetrics.onlineMapMatchingMatches = onlineMapMatchingStats.pointsMatched;
    runMetrics.onlineMapMatchingInitializations = onlineMapMatchingStats.initializations;
    runMetrics.onlineMapMatchingPredictionCandidates =
      onlineMapMatchingStats.predictionCandidates;
    runMetrics.onlineMapMatchingPrunedCandidates =
      onlineMapMatchingStats.prunedCandidates;
    runMetrics.onlineMapMatchingPredictionBranches =
      onlineMapMatchingStats.predictionBranches;

    std::vector<double> segmentCompressionMillis(preparedSegments.size(), 0.0);
    while (!arrivalEvents.empty()) {
      const TraceArrivalEvent event = arrivalEvents.top();
      arrivalEvents.pop();
      PreparedTraceSegment& prepared = preparedSegments.at(event.preparedIndex);
      const Clock::time_point eventStart = Clock::now();
      traceCodec.pushOnlineEdges(
        prepared.spatialTrajectoryId,
        prepared.encodedArrivalChunks.at(event.arrivalIndex),
        event.timestamp
      );
      std::vector<int> arrivedSpeed;
      if (event.arrivalIndex > 0) {
        arrivedSpeed.push_back((int)prepared.originalTemporal.quantizedSpeeds.at(
          event.arrivalIndex - 1
        ));
      }
      speedCodec.pushOnlineEdges(
        prepared.speedTrajectoryId,
        arrivedSpeed,
        event.timestamp
      );
      segmentCompressionMillis.at(event.preparedIndex) +=
        std::chrono::duration_cast<std::chrono::duration<double, std::milli> >(
          Clock::now() - eventStart
        ).count();

      if (event.arrivalIndex + 1 != prepared.encodedArrivalChunks.size()) {
        TraceArrivalEvent nextEvent;
        nextEvent.timestamp = prepared.traceInput.getTemporalComponent().at(
          event.arrivalIndex + 1
        ).t;
        nextEvent.preparedIndex = event.preparedIndex;
        nextEvent.arrivalIndex = event.arrivalIndex + 1;
        nextEvent.stableOrder = prepared.arrivalOrderBase + nextEvent.arrivalIndex;
        arrivalEvents.push(nextEvent);
        continue;
      }
      TraceCompressedTrajectory compressed;
      TraceCompressedTrajectory compressedSpeeds;
      const Clock::time_point finishStart = Clock::now();
      traceCodec.finishOnlineTrajectory(prepared.spatialTrajectoryId, compressed);
      speedCodec.finishOnlineTrajectory(prepared.speedTrajectoryId, compressedSpeeds);
      segmentCompressionMillis.at(event.preparedIndex) +=
        std::chrono::duration_cast<std::chrono::duration<double, std::milli> >(
          Clock::now() - finishStart
        ).count();
      finishPreparedSegment(
        prepared,
        compressed,
        compressedSpeeds,
        speedCodec,
        utcqCodec,
        traceBitCodec,
        options,
        runMetrics,
        outputPackets,
        segmentCompressionMillis.at(event.preparedIndex)
      );
      PreparedTraceSegment released;
      std::swap(prepared, released);
    }


    const TraceStreamPacket outputStream =
      traceBitCodec.encodePaperStream(outputPackets);
    runMetrics.outputBitstreamBytes = outputStream.bytes.size();
    runMetrics.outputBitstreamMeaningfulBits = outputStream.meaningfulBits;
    if (!options.bitstreamPath.empty()) {
      std::ofstream bitstreamOutput(
        options.bitstreamPath.c_str(),
        std::ios::binary | std::ios::out | std::ios::trunc
      );
      if (!bitstreamOutput.good()) {
        throw std::runtime_error("TRACE failed to open bitstream output.");
      }
      if (!outputStream.bytes.empty()) {
        bitstreamOutput.write(
          reinterpret_cast<const char*>(&outputStream.bytes.at(0)),
          (std::streamsize)outputStream.bytes.size()
        );
      }
      if (!bitstreamOutput.good()) {
        throw std::runtime_error("TRACE failed to write bitstream output.");
      }
    }

    TraceSummaryInput summaryInput;
    summaryInput.dataset = options.dataset;
    summaryInput.metrics = runMetrics;
    summaryInput.traceParameters = traceOptions;
    summaryInput.runtimeStats = traceCodec.getRuntimeStats();
    summaryInput.referenceCount = traceCodec.getReferences().size();
    summaryInput.referenceEdges = traceCodec.getTotalReferenceEdges();
    summaryInput.speedReferenceCount = speedCodec.getReferences().size();
    summaryInput.speedReferenceValues = speedCodec.getTotalReferenceEdges();
    summaryInput.encodingGamma = options.encodingGamma;
    summaryInput.speedErrorEta = options.speedErrorEta;
    std::string summary = buildTraceSummaryJson(summaryInput);

    if (!options.summaryPath.empty()) {
      std::ofstream summaryFile(options.summaryPath.c_str());
      summaryFile << summary;
    }
    std::cout << summary;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "TRACE runner failed: " << ex.what() << std::endl;
    return 1;
  } catch (const char* ex) {
    std::cerr << "TRACE runner failed: " << ex << std::endl;
    return 1;
  }
}
