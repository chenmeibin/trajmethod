//
//  graph_reader_press_roadnet.hpp
//  press-v3
//

#ifndef graph_reader_press_roadnet_hpp
#define graph_reader_press_roadnet_hpp

#include "./graph_reader_base.hpp"

class GraphReaderPressRoadnet: public GraphReaderBase {
public:
  void readGraphImpl(const std::string& fileName, Graph& graph);
};

#endif /* graph_reader_press_roadnet_hpp */