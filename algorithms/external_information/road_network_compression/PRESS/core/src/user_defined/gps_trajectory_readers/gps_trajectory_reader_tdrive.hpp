//
//  gps_trajectory_reader_tdrive.hpp
//  press-v3
//

#ifndef gps_trajectory_reader_tdrive_hpp
#define gps_trajectory_reader_tdrive_hpp

#include "./gps_trajectory_reader_base.hpp"

class GPSTrajectoryReaderTDrive: public GPSTrajectoryReaderBase {
public:
  void readGPSTrajectoryImpl(const std::string& fileName, GPSTrajectory& gpsTrajectory);
};

#endif /* gps_trajectory_reader_tdrive_hpp */