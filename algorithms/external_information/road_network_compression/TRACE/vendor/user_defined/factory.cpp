//
//  factory.cpp
//  press-v3
//
//  Created by Renchu Song on 9/6/20.
//  Copyright © 2020 Renc. All rights reserved.
//

#include "factory.hpp"
#include <cassert>
#include <string>

#include "../third_party/log.h"

#include "./gps_trajectory_readers/gps_trajectory_reader_seattle_sample.hpp"
#include "./gps_trajectory_readers/gps_trajectory_reader_tdrive.hpp"
#include "./gps_trajectory_readers/gps_trajectory_reader_chengdu.hpp"
#include "./gps_trajectory_readers/gps_trajectory_reader_geolife.hpp"
// [USER DEFINE]: Include your own gps trajectory reader types here.

#include "./graph_readers/graph_reader_seattle_sample.hpp"
#include "./graph_readers/graph_reader_press_roadnet.hpp"
// [USER DEFINE]: Include your own graph reader types here.

// Get graph reader type from string name.
GraphReaderType getGraphReaderType(const std::string& graphReaderType) {
  if (graphReaderType == "SEATTLE_SAMPLE_ROADNET") {
    return GraphReaderType::SEATTLE_SAMPLE_ROADNET;
  }
  if (graphReaderType == "PRESS_ROADNET") {
    return GraphReaderType::PRESS_ROADNET;
  }
  // [USER DEFINE]: Define your own graph reader types mapping here.
  
  FILE_LOG(TLogLevel::lerror) << "Undefined graph reader type: " << graphReaderType;
  throw "Undefined graph reader type";
}

// Get GPS trajectory reader type from string name.
GPSTrajectoryReaderType getGPSTrajectoryReaderType(const std::string& gpsTrajectoryReaderType) {
  if (gpsTrajectoryReaderType == "SEATTLE_SAMPLE_GPS") {
    return GPSTrajectoryReaderType::SEATTLE_SAMPLE_GPS;
  }
  if (gpsTrajectoryReaderType == "TDRIVE_GPS") {
    return GPSTrajectoryReaderType::TDRIVE_GPS;
  }
  if (gpsTrajectoryReaderType == "CHENGDU_GPS") {
    return GPSTrajectoryReaderType::CHENGDU_GPS;
  }
  if (gpsTrajectoryReaderType == "GEOLIFE_GPS") {
    return GPSTrajectoryReaderType::GEOLIFE_GPS;
  }
  // [USER DEFINE]: Define your own GPS trajectory reader types here.
  
  FILE_LOG(TLogLevel::lerror) << "Undefined gps trajectory reader type: "
    << gpsTrajectoryReaderType;
  throw "Undefined gps trajectory reader type";
}

std::shared_ptr<GraphReaderBase> Factory::getGraphReader(const GraphReaderType grType) {
  switch (grType) {
    case GraphReaderType::SEATTLE_SAMPLE_ROADNET: {
      return std::make_shared<GraphReaderSeattleSample>();
    }
    case GraphReaderType::PRESS_ROADNET: {
      return std::make_shared<GraphReaderPressRoadnet>();
    }
    // [USER DEFINE]: Construct your own graph reader types here.
  }
  
  // Should not go to this point. Check above if you handled all graph reader types.
  throw "Undefined graph reader type";
}

std::shared_ptr<GPSTrajectoryReaderBase> Factory::getGPSTrajectoryReader(
  const GPSTrajectoryReaderType grType
) {
  switch (grType) {
    case GPSTrajectoryReaderType::SEATTLE_SAMPLE_GPS: {
      return std::make_shared<GPSTrajectoryReaderSeattleSample>();
    }
    case GPSTrajectoryReaderType::TDRIVE_GPS: {
      return std::make_shared<GPSTrajectoryReaderTDrive>();
    }
    case GPSTrajectoryReaderType::CHENGDU_GPS: {
      return std::make_shared<GPSTrajectoryReaderChengdu>();
    }
    case GPSTrajectoryReaderType::GEOLIFE_GPS: {
      return std::make_shared<GPSTrajectoryReaderGeolife>();
    }
    // [USER DEFINE]: Construct your own gps trajectory reader types here.
  }
  
  // Should not go to this point. Check above if you handled all gps trajectory reader types.
  throw "Undefined gps trajectory reader type";
}
