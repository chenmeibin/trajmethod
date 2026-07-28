//
//  gps_trajectory_reader_chengdu.cpp
//  press-v3
//

#include "gps_trajectory_reader_chengdu.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool parseChengduLine(
  const std::string& line,
  double& timestamp,
  double& latitude,
  double& longitude
) {
  std::stringstream lineStream(line);
  std::string timestampStr;
  std::string longitudeStr;
  std::string latitudeStr;

  if (!std::getline(lineStream, timestampStr, ',')) {
    return false;
  }
  if (!std::getline(lineStream, longitudeStr, ',')) {
    return false;
  }
  if (!std::getline(lineStream, latitudeStr, ',')) {
    return false;
  }

  std::tm timeInfo = {};
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (std::sscanf(
    timestampStr.c_str(),
    "%d-%d-%d %d:%d:%d",
    &year,
    &month,
    &day,
    &hour,
    &minute,
    &second
  ) != 6) {
    return false;
  }

  timeInfo.tm_year = year - 1900;
  timeInfo.tm_mon = month - 1;
  timeInfo.tm_mday = day;
  timeInfo.tm_hour = hour;
  timeInfo.tm_min = minute;
  timeInfo.tm_sec = second;

  timestamp = mktime(&timeInfo);
  longitude = std::stod(longitudeStr);
  latitude = std::stod(latitudeStr);
  return true;
}

}

void GPSTrajectoryReaderChengdu::readGPSTrajectoryImpl(
  const std::string& fileName,
  GPSTrajectory& gpsTrajectory
) {
  std::ifstream input(fileName);
  if (!input.is_open()) {
    throw "Failed to open Chengdu trajectory file";
  }

  std::vector<GPSPoint> gpsPoints;
  std::string line;
  while (std::getline(input, line)) {
    double timestamp = 0;
    double latitude = 0;
    double longitude = 0;
    if (parseChengduLine(line, timestamp, latitude, longitude)) {
      gpsPoints.emplace_back(GPSPoint(timestamp, latitude, longitude));
    }
  }

  gpsTrajectory.setGPSTrajectory(gpsPoints);
}