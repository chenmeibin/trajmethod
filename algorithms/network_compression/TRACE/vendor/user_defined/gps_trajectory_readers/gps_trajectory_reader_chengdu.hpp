//
//  gps_trajectory_reader_chengdu.hpp
//  press-v3
//

#ifndef gps_trajectory_reader_chengdu_hpp
#define gps_trajectory_reader_chengdu_hpp

#include "./gps_trajectory_reader_base.hpp"

class GPSTrajectoryReaderChengdu: public GPSTrajectoryReaderBase {
public:
  void readGPSTrajectoryImpl(const std::string& fileName, GPSTrajectory& gpsTrajectory);
};

#endif /* gps_trajectory_reader_chengdu_hpp */