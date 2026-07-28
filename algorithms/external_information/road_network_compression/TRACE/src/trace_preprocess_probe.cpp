#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "../vendor_press/io/file_reader.hpp"
#include "../vendor_press/io/file_writer.hpp"
#include "../vendor_press/service/map_matcher.hpp"
#include "../vendor_press/topology/grid_index.hpp"
#include "../vendor_press/topology/sp_table.hpp"
#include "../vendor_press/topology/trajectory.hpp"
#include "../vendor_press/user_defined/factory.hpp"
#include "../vendor_press/util/helper.hpp"

namespace {

struct Options {
  std::string root = ".";
  std::string dataset = "T-drive";
  std::string gpsFile = "T-drive/313.txt";
  double maxSpDist = 4000.0;
  double gridWidth = 50.0;
  double gridHeight = 50.0;
};

std::string ensureTrailingSlash(const std::string& path) {
  if (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
    return path;
  }
  return path + "/";
}

std::string joinPath(const std::string& left, const std::string& right) {
  return ensureTrailingSlash(left) + right;
}

Options parseArgs(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      options.root = argv[++i];
    } else if (arg == "--dataset" && i + 1 < argc) {
      options.dataset = argv[++i];
    } else if (arg == "--gps-file" && i + 1 < argc) {
      options.gpsFile = argv[++i];
    } else if (arg == "--max-sp-dist" && i + 1 < argc) {
      options.maxSpDist = std::atof(argv[++i]);
    } else if (arg == "--grid-width" && i + 1 < argc) {
      options.gridWidth = std::atof(argv[++i]);
    } else if (arg == "--grid-height" && i + 1 < argc) {
      options.gridHeight = std::atof(argv[++i]);
    } else {
      throw std::runtime_error("Unknown or incomplete argument: " + arg);
    }
  }
  return options;
}

uint64_t mixHash(uint64_t seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
  return seed;
}

uint64_t hashDouble(double value) {
  union {
    double d;
    uint64_t u;
  } converter;
  converter.d = value;
  return converter.u;
}

uint64_t hashGraph(const Graph& graph) {
  uint64_t seed = 1469598103934665603ULL;
  seed = mixHash(seed, graph.getNodeNumber());
  seed = mixHash(seed, graph.getEdgeNumber());
  for (size_t nodeIndex = 0; nodeIndex < graph.getNodeNumber(); ++nodeIndex) {
    const Node& node = graph.getNode(nodeIndex);
    seed = mixHash(seed, hashDouble(node.getPosition().x));
    seed = mixHash(seed, hashDouble(node.getPosition().y));
    seed = mixHash(seed, node.getEdgeNumber());
    for (size_t edgeIndex = 0; edgeIndex < node.getEdgeNumber(); ++edgeIndex) {
      seed = mixHash(seed, (uint64_t)node.getEdgeId(edgeIndex));
    }
  }
  for (size_t edgeIndex = 0; edgeIndex < graph.getEdgeNumber(); ++edgeIndex) {
    const Edge& edge = graph.getEdge(edgeIndex);
    seed = mixHash(seed, (uint64_t)edge.getSourceId());
    seed = mixHash(seed, (uint64_t)edge.getTargetId());
    seed = mixHash(seed, edge.getGeoSize());
    for (size_t shapeIndex = 0; shapeIndex < edge.getGeoSize(); ++shapeIndex) {
      seed = mixHash(seed, hashDouble(edge.getShape().at(shapeIndex).x));
      seed = mixHash(seed, hashDouble(edge.getShape().at(shapeIndex).y));
    }
  }
  return seed;
}

void buildPreprocessing(
  const Options& options,
  Graph& graph,
  GridIndex& gridIndex,
  SPTable& spTable
) {
  const std::string roadNetworkText =
    joinPath(joinPath(joinPath(options.root, "data"), options.dataset), "road_network.txt");
  std::shared_ptr<GraphReaderBase> graphReader =
    Factory::getGraphReader(GraphReaderType::PRESS_ROADNET);
  graphReader->readGraph(roadNetworkText, graph);
  gridIndex.build(graph, options.gridWidth, options.gridHeight);
  spTable.build(graph, options.maxSpDist);
}

void loadBinaries(
  const Options& options,
  Graph& graph,
  GridIndex& gridIndex,
  SPTable& spTable
) {
  const std::string expRoot = joinPath(joinPath(options.root, "experiments"), options.dataset);
  FileReader graphReader(joinPath(expRoot, "road_network.bin").c_str(), true);
  FileReader gridReader(joinPath(expRoot, "grid_index.bin").c_str(), true);
  FileReader spReader(joinPath(expRoot, "sp_table.bin").c_str(), true);
  graph.load(graphReader);
  gridIndex.load(gridReader);
  spTable.load(spReader);
}

void writeTempBinaries(
  const std::string& tempRoot,
  Graph& graph,
  GridIndex& gridIndex,
  SPTable& spTable
) {
  FileWriter graphWriter(joinPath(tempRoot, "road_network.bin").c_str(), true);
  graph.store(graphWriter);
  FileWriter gridWriter(joinPath(tempRoot, "grid_index.bin").c_str(), true);
  gridIndex.store(gridWriter);
  FileWriter spWriter(joinPath(tempRoot, "sp_table.bin").c_str(), true);
  spTable.store(spWriter);
}

void loadTempBinaries(
  const std::string& tempRoot,
  Graph& graph,
  GridIndex& gridIndex,
  SPTable& spTable
) {
  FileReader graphReader(joinPath(tempRoot, "road_network.bin").c_str(), true);
  FileReader gridReader(joinPath(tempRoot, "grid_index.bin").c_str(), true);
  FileReader spReader(joinPath(tempRoot, "sp_table.bin").c_str(), true);
  graph.load(graphReader);
  gridIndex.load(gridReader);
  spTable.load(spReader);
}

std::vector<int> searchEdges(
  const GridIndex& gridIndex,
  const GPSPoint& gpsPoint,
  double radius
) {
  std::unordered_set<int> edges;
  gridIndex.search(gps2Point2D(gpsPoint), radius, edges);
  std::vector<int> result(edges.begin(), edges.end());
  std::sort(result.begin(), result.end());
  return result;
}

void printEdgeVector(const std::vector<int>& edges) {
  std::cout << "[";
  for (size_t i = 0; i < edges.size(); ++i) {
    if (i > 0) {
      std::cout << ", ";
    }
    std::cout << edges.at(i);
  }
  std::cout << "]";
}

void compareGpsSearches(
  const Options& options,
  const GridIndex& builtGrid,
  const GridIndex& loadedGrid
) {
  GPSTrajectory gpsTrajectory;
  std::shared_ptr<GPSTrajectoryReaderBase> gpsReader =
    Factory::getGPSTrajectoryReader(getGPSTrajectoryReaderType("TDRIVE_GPS"));
  gpsReader->readGPSTrajectory(joinPath(joinPath(options.root, "data"), options.gpsFile), gpsTrajectory);

  const size_t sampleCount = std::min<size_t>(gpsTrajectory.getLength(), 8);
  std::cout << "gps_probe_file=" << options.gpsFile << " gps_points=" << gpsTrajectory.getLength() << "\n";
  for (size_t i = 0; i < sampleCount; ++i) {
    const GPSPoint& point = gpsTrajectory.getGPSPoint((int)i);
    std::vector<int> built = searchEdges(builtGrid, point, 50.0);
    std::vector<int> loaded = searchEdges(loadedGrid, point, 50.0);
    bool same = (built == loaded);
    std::cout
      << "grid_search point=" << i
      << " built_count=" << built.size()
      << " loaded_count=" << loaded.size()
      << " same=" << (same ? "true" : "false")
      << "\n";
    if (!same) {
      std::cout << " built=";
      printEdgeVector(built);
      std::cout << "\n loaded=";
      printEdgeVector(loaded);
      std::cout << "\n";
    }
  }
}

void compareSampleShortestPaths(
  const Graph& graph,
  const SPTable& builtSp,
  const SPTable& loadedSp
) {
  struct NodePair {
    int src;
    int dst;
  };
  std::vector<NodePair> pairs;
  pairs.push_back(NodePair{0, 1});
  pairs.push_back(NodePair{1, 10});
  pairs.push_back(NodePair{10, 100});
  pairs.push_back(NodePair{697, 1409});
  pairs.push_back(NodePair{1409, 697});

  for (size_t i = 0; i < pairs.size(); ++i) {
    const NodePair& pair = pairs.at(i);
    if (pair.src < 0 || pair.dst < 0 ||
        pair.src >= (int)graph.getNodeNumber() ||
        pair.dst >= (int)graph.getNodeNumber()) {
      continue;
    }
    int builtPrev = builtSp.prevEdgeIndex((size_t)pair.src, (size_t)pair.dst);
    int loadedPrev = loadedSp.prevEdgeIndex((size_t)pair.src, (size_t)pair.dst);
    std::cout
      << "sp_prev src=" << pair.src
      << " dst=" << pair.dst
      << " built=" << builtPrev
      << " loaded=" << loadedPrev
      << " same=" << (builtPrev == loadedPrev ? "true" : "false")
      << "\n";
  }
}

void compareGraphDetails(const Graph& builtGraph, const Graph& loadedGraph) {
  bool reportedNodeDiff = false;
  const size_t nodeCount = std::min(builtGraph.getNodeNumber(), loadedGraph.getNodeNumber());
  for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
    const Node& builtNode = builtGraph.getNode(nodeIndex);
    const Node& loadedNode = loadedGraph.getNode(nodeIndex);
    if (builtNode.getPosition().x != loadedNode.getPosition().x ||
        builtNode.getPosition().y != loadedNode.getPosition().y ||
        builtNode.getEdgeNumber() != loadedNode.getEdgeNumber()) {
      std::cout
        << "node_diff index=" << nodeIndex
        << " built_pos=(" << std::setprecision(17) << builtNode.getPosition().x << ","
        << builtNode.getPosition().y << ")"
        << " loaded_pos=(" << loadedNode.getPosition().x << "," << loadedNode.getPosition().y << ")"
        << " built_deg=" << builtNode.getEdgeNumber()
        << " loaded_deg=" << loadedNode.getEdgeNumber()
        << "\n";
      reportedNodeDiff = true;
      break;
    }
    for (size_t edgeOffset = 0; edgeOffset < builtNode.getEdgeNumber(); ++edgeOffset) {
      if (builtNode.getEdgeId(edgeOffset) != loadedNode.getEdgeId(edgeOffset)) {
        std::cout
          << "node_edge_list_diff index=" << nodeIndex
          << " offset=" << edgeOffset
          << " built_edge=" << builtNode.getEdgeId(edgeOffset)
          << " loaded_edge=" << loadedNode.getEdgeId(edgeOffset)
          << "\n";
        reportedNodeDiff = true;
        break;
      }
    }
    if (reportedNodeDiff) {
      break;
    }
  }

  bool reportedEdgeDiff = false;
  const size_t edgeCount = std::min(builtGraph.getEdgeNumber(), loadedGraph.getEdgeNumber());
  for (size_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
    const Edge& builtEdge = builtGraph.getEdge(edgeIndex);
    const Edge& loadedEdge = loadedGraph.getEdge(edgeIndex);
    if (builtEdge.getSourceId() != loadedEdge.getSourceId() ||
        builtEdge.getTargetId() != loadedEdge.getTargetId() ||
        builtEdge.getGeoSize() != loadedEdge.getGeoSize()) {
      std::cout
        << "edge_diff index=" << edgeIndex
        << " built=(" << builtEdge.getSourceId() << "," << builtEdge.getTargetId()
        << "," << builtEdge.getGeoSize() << ")"
        << " loaded=(" << loadedEdge.getSourceId() << "," << loadedEdge.getTargetId()
        << "," << loadedEdge.getGeoSize() << ")"
        << "\n";
      reportedEdgeDiff = true;
      break;
    }
    for (size_t pointIndex = 0; pointIndex < builtEdge.getGeoSize(); ++pointIndex) {
      const Point2D& builtPoint = builtEdge.getShape().at(pointIndex);
      const Point2D& loadedPoint = loadedEdge.getShape().at(pointIndex);
      if (builtPoint.x != loadedPoint.x || builtPoint.y != loadedPoint.y) {
        std::cout
          << "edge_shape_diff index=" << edgeIndex
          << " point=" << pointIndex
          << " built=(" << std::setprecision(17) << builtPoint.x << "," << builtPoint.y << ")"
          << " loaded=(" << loadedPoint.x << "," << loadedPoint.y << ")"
          << "\n";
        reportedEdgeDiff = true;
        break;
      }
    }
    if (reportedEdgeDiff) {
      break;
    }
  }

  if (!reportedNodeDiff && !reportedEdgeDiff) {
    std::cout << "graph_detail_diff=none_detected\n";
  }
}

void compareMapMatchOutputs(
  const Options& options,
  const Graph& builtGraph,
  const GridIndex& builtGrid,
  const SPTable& builtSp,
  const Graph& loadedGraph,
  const GridIndex& loadedGrid,
  const SPTable& loadedSp
) {
  GPSTrajectory gpsTrajectory;
  std::shared_ptr<GPSTrajectoryReaderBase> gpsReader =
    Factory::getGPSTrajectoryReader(getGPSTrajectoryReaderType("TDRIVE_GPS"));
  gpsReader->readGPSTrajectory(joinPath(joinPath(options.root, "data"), options.gpsFile), gpsTrajectory);

  MapMatcher matcher;
  std::vector<GPSTrajectory> builtGpsSegments;
  std::vector<MapMatchedTrajectory> builtMmSegments;
  matcher.mapMatch(
    builtSp,
    builtGraph,
    builtGrid,
    4.07,
    50.0,
    2000.0,
    gpsTrajectory,
    builtGpsSegments,
    builtMmSegments
  );

  std::vector<GPSTrajectory> loadedGpsSegments;
  std::vector<MapMatchedTrajectory> loadedMmSegments;
  matcher.mapMatch(
    loadedSp,
    loadedGraph,
    loadedGrid,
    4.07,
    50.0,
    2000.0,
    gpsTrajectory,
    loadedGpsSegments,
    loadedMmSegments
  );

  std::cout
    << "map_match_segments built=" << builtMmSegments.size()
    << " loaded=" << loadedMmSegments.size()
    << "\n";
  if (!builtMmSegments.empty()) {
    std::cout
      << "map_match_first_lengths built_gps=" << builtGpsSegments.at(0).getLength()
      << " built_mm=" << builtMmSegments.at(0).getLength()
      << "\n";
  }
  if (!loadedMmSegments.empty()) {
    std::cout
      << "map_match_first_lengths loaded_gps=" << loadedGpsSegments.at(0).getLength()
      << " loaded_mm=" << loadedMmSegments.at(0).getLength()
      << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parseArgs(argc, argv);

    Graph builtGraph;
    GridIndex builtGrid;
    SPTable builtSp;
    buildPreprocessing(options, builtGraph, builtGrid, builtSp);

    const uint64_t builtHash = hashGraph(builtGraph);
    std::cout
      << "built_graph nodes=" << builtGraph.getNodeNumber()
      << " edges=" << builtGraph.getEdgeNumber()
      << " hash=" << builtHash
      << "\n";

    Graph loadedGraph;
    GridIndex loadedGrid;
    SPTable loadedSp;
    loadBinaries(options, loadedGraph, loadedGrid, loadedSp);

    const uint64_t loadedHash = hashGraph(loadedGraph);
    std::cout
      << "loaded_graph nodes=" << loadedGraph.getNodeNumber()
      << " edges=" << loadedGraph.getEdgeNumber()
      << " hash=" << loadedHash
      << "\n";

    std::cout << "graph_same=" << (builtHash == loadedHash ? "true" : "false") << "\n";

    compareGraphDetails(builtGraph, loadedGraph);
    compareGpsSearches(options, builtGrid, loadedGrid);
    compareSampleShortestPaths(builtGraph, builtSp, loadedSp);
    compareMapMatchOutputs(options, builtGraph, builtGrid, builtSp, loadedGraph, loadedGrid, loadedSp);

    const std::string tempRoot = joinPath(joinPath(options.root, "trace"), "output/preprocess_probe_tmp");
    std::system((std::string("if not exist \"") + tempRoot + "\" mkdir \"" + tempRoot + "\"").c_str());
    writeTempBinaries(tempRoot, builtGraph, builtGrid, builtSp);
    Graph tempGraph;
    GridIndex tempGrid;
    SPTable tempSp;
    loadTempBinaries(tempRoot, tempGraph, tempGrid, tempSp);
    std::cout
      << "temp_roundtrip_graph hash=" << hashGraph(tempGraph)
      << " same_as_built=" << (hashGraph(tempGraph) == builtHash ? "true" : "false")
      << "\n";
    compareGraphDetails(builtGraph, tempGraph);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "trace_preprocess_probe failed: " << error.what() << std::endl;
    return 1;
  } catch (const char* error) {
    std::cerr << "trace_preprocess_probe failed: " << error << std::endl;
    return 1;
  }
}
