//
//  gps_trajectory_reader_geolife.hpp
//  press-v3
//

#ifndef gps_trajectory_reader_geolife_hpp
#define gps_trajectory_reader_geolife_hpp

#include "./gps_trajectory_reader_base.hpp"

class GPSTrajectoryReaderGeolife: public GPSTrajectoryReaderBase {
public:
  void readGPSTrajectoryImpl(const std::string& fileName, GPSTrajectory& gpsTrajectory);
};

#endif /* gps_trajectory_reader_geolife_hpp */