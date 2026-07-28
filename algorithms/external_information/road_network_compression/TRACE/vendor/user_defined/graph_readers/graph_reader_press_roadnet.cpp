//
//  graph_reader_press_roadnet.cpp
//  press-v3
//

#include <algorithm>

#include "graph_reader_press_roadnet.hpp"

#include "../../io/file_reader.hpp"
#include "../../util/helper.hpp"

void GraphReaderPressRoadnet::readGraphImpl(const std::string& fileName, Graph& graph) {
  std::vector<Node> nodeList;
  std::vector<Edge> edgeList;

  FileReader reader(fileName.c_str(), false);

  do {} while (reader.nextChar() != '\n');

  std::unordered_map<long long, int> nodeIdToIndex;

  int nodeNumber = 0;
  int edgeNumber = 0;
  long long edgeId;
  while ((edgeId = reader.nextLong()) != EOF) {
    long long srcRawId = reader.nextLong();
    long long tgtRawId = reader.nextLong();
    bool twoWay = (reader.nextInt() == 1);
    reader.nextDouble();
    int shapeSize = reader.nextInt();
    do {} while (reader.nextChar() != '(');
    std::vector<Point2D> shape;
    for (auto i = 0; i < shapeSize; ++i) {
      if (i > 0) {
        reader.nextChar();
      }
      double lng = reader.nextDouble();
      double lat = reader.nextDouble();
      GPSPoint gps(lat, lng);
      shape.emplace_back(gps2Point2D(gps));
    }
    reader.nextChar();

    int srcId;
    if (nodeIdToIndex.find(srcRawId) != nodeIdToIndex.end()) {
      srcId = nodeIdToIndex.at(srcRawId);
    } else {
      nodeList.emplace_back(Node(shape.at(0)));
      srcId = nodeNumber++;
      nodeIdToIndex[srcRawId] = srcId;
    }

    int tgtId;
    if (nodeIdToIndex.find(tgtRawId) != nodeIdToIndex.end()) {
      tgtId = nodeIdToIndex.at(tgtRawId);
    } else {
      nodeList.emplace_back(Node(shape.at(shape.size() - 1)));
      tgtId = nodeNumber++;
      nodeIdToIndex[tgtRawId] = tgtId;
    }

    Edge forward(srcId, tgtId);
    for (auto& point: shape) {
      forward.addPosition(point);
    }
    edgeList.emplace_back(forward);
    graph.eid2OriginalId[edgeNumber] = edgeId;
    graph.fromTo[edgeNumber] = 1;
    nodeList.at(srcId).addEdge(edgeNumber++);

    if (twoWay) {
      Edge backward(tgtId, srcId);
      std::reverse(shape.begin(), shape.end());
      for (auto& point: shape) {
        backward.addPosition(point);
      }
      edgeList.emplace_back(backward);
      graph.eid2OriginalId[edgeNumber] = edgeId;
      graph.fromTo[edgeNumber] = 0;
      nodeList.at(tgtId).addEdge(edgeNumber++);
    }
  }

  graph.setGraph(nodeList, edgeList);
}