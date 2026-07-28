//
//  gps_trajectory_reader_geolife.cpp
//  press-v3
//

#include "gps_trajectory_reader_geolife.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool parseGeolifeLine(
  const std::string& line,
  double& timestamp,
  double& latitude,
  double& longitude
) {
  std::stringstream lineStream(line);
  std::string latitudeStr;
  std::string longitudeStr;
  std::string skipField;
  std::string dateStr;
  std::string timeStr;

  if (!std::getline(lineStream, latitudeStr, ',')) {
    return false;
  }
  if (!std::getline(lineStream, longitudeStr, ',')) {
    return false;
  }
  if (!std::getline(lineStream, skipField, ',')) {
    return false;
  }
  if (!std::getline(lineStream, skipField, ',')) {
    return false;
  }
  if (!std::getline(lineStream, skipField, ',')) {
    return false;
  }
  if (!std::getline(lineStream, dateStr, ',')) {
    return false;
  }
  if (!std::getline(lineStream, timeStr, ',')) {
    return false;
  }

  std::tm timeInfo = {};
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  std::string dateTimeStr = dateStr + " " + timeStr;
  if (std::sscanf(
    dateTimeStr.c_str(),
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
  latitude = std::stod(latitudeStr);
  longitude = std::stod(longitudeStr);
  return true;
}

}

void GPSTrajectoryReaderGeolife::readGPSTrajectoryImpl(
  const std::string& fileName,
  GPSTrajectory& gpsTrajectory
) {
  std::ifstream input(fileName);
  if (!input.is_open()) {
    throw "Failed to open Geolife trajectory file";
  }

  std::vector<GPSPoint> gpsPoints;
  std::string line;
  while (std::getline(input, line)) {
    double timestamp = 0;
    double latitude = 0;
    double longitude = 0;
    if (parseGeolifeLine(line, timestamp, latitude, longitude)) {
      gpsPoints.emplace_back(GPSPoint(timestamp, latitude, longitude));
    }
  }

  gpsTrajectory.setGPSTrajectory(gpsPoints);
}