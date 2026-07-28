//
//  main.cpp
//  press-v3
//
//  Created by Renchu Song on 11/23/20.
//  Copyright © 2018 Renc. All rights reserved.
//

#include <iostream>
#include <csignal>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */
#include <thread>
#include <chrono>
#include <vector>

#include "io/file_reader.hpp"
#include "third_party/picojson.h"
#include "topology/trajectory.hpp"
#include "util/utility.hpp"
#include "util/helper.hpp"
#include "util/timer.hpp"

struct StageResult {
  std::string label;
  long long milliseconds;
  picojson::value responseJson;

  StageResult(const std::string& label, long long milliseconds, const picojson::value& responseJson)
    : label(label), milliseconds(milliseconds), responseJson(responseJson) { }
};

struct ReqRespHelper {
  std::string inPath;
  std::string outPath;
  std::string data;
  std::string response;
  std::string lastResponse;

  ReqRespHelper(char* inPath, const char* outPath): inPath(inPath), outPath(outPath) { }

  std::string& readNext() {
    while (true) {
      std::ifstream reqStream;
      reqStream.open(inPath, std::ifstream::in);
      if (!reqStream.is_open()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      std::getline(reqStream, data);
      reqStream.close();
      if (!data.empty() && data != lastResponse) {
        lastResponse = data;
        return data;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void writeNext(std::string data) {
    std::ofstream clearResponseStream;
    clearResponseStream.open(inPath, std::ofstream::out | std::ofstream::trunc);
    clearResponseStream.close();
    lastResponse.clear();

    std::ofstream respStream;
    respStream.open(outPath, std::ofstream::out | std::ofstream::trunc);
    if (!respStream.is_open()) {
      std::cout << "Fail to open response stream.";
      exit(EXIT_FAILURE);
    }
    respStream << data << std::endl;
    respStream.close();
  }

  picojson::value explainResponse(std::string& response) {
    // Parse response.
    picojson::value responseJson;
    std::string err = picojson::parse(responseJson, response);
    if (!err.empty()) {
      std::cout << "Failed to parse response (" << response << ")";
      return responseJson;
    }
    if (responseJson.get("Success").get<bool>()) {
      if (responseJson.contains("Message")) {
        std::cout << responseJson.get("Message").get<std::string>() << std::endl;
      }
      if (responseJson.contains("Data")) {
        std::cout << responseJson.get("Data") << std::endl;
      }
    } else {
      std::cout << "Failed " << responseJson.get("Message").get<std::string>() << std::endl;
    }
    return responseJson;
  }
};

std::string queryPayload(std::string cmd, std::string query) {
  return "{\"Cmd\":\"" + cmd + "\", \"Query\":" + query + "}";
}

namespace {

const char* kDefaultDatasetFolder = "T-drive";
const char* kDefaultGraphReaderType = "PRESS_ROADNET";
const char* kDefaultTDriveReaderType = "TDRIVE_GPS";
const char* kDefaultChengduReaderType = "CHENGDU_GPS";
const char* kDefaultGeolifeReaderType = "GEOLIFE_GPS";

bool isFlag(const std::string& value, const char* expected) {
  return value == expected;
}

bool isChengduDataset(const std::string& datasetFolder) {
  return datasetFolder == "chengdu" || datasetFolder == "Chengdu";
}

bool isGeolifeDataset(const std::string& datasetFolder) {
  return datasetFolder == "Geolife" || datasetFolder == "geolife" || datasetFolder == "GeoLife";
}

std::string defaultGPSReaderTypeForDataset(const std::string& datasetFolder) {
  if (isChengduDataset(datasetFolder)) {
    return kDefaultChengduReaderType;
  }
  if (isGeolifeDataset(datasetFolder)) {
    return kDefaultGeolifeReaderType;
  }
  return kDefaultTDriveReaderType;
}

std::string defaultGPSFileForDataset(const std::string& datasetFolder) {
  if (isChengduDataset(datasetFolder)) {
    return datasetFolder + "/1.txt";
  }
  if (isGeolifeDataset(datasetFolder)) {
    return datasetFolder + "/20081103133204.plt";
  }
  return datasetFolder + "/10_short.txt";
}

std::string trimTrailingPeriod(std::string value) {
  if (!value.empty() && value.at(value.size() - 1) == '.') {
    value.erase(value.size() - 1);
  }
  return value;
}

std::string extractResponseMessage(const picojson::value& responseJson) {
  if (!responseJson.contains("Message")) {
    return "";
  }
  return responseJson.get("Message").get<std::string>();
}

std::string extractDumpFolder(const picojson::value& responseJson) {
  std::string message = extractResponseMessage(responseJson);
  std::string marker = " to ";
  std::size_t markerPos = message.rfind(marker);
  if (markerPos == std::string::npos) {
    return "";
  }
  return trimTrailingPeriod(message.substr(markerPos + marker.size()));
}

std::string ensureTrailingSlash(std::string path) {
  if (!path.empty() && path.at(path.size() - 1) != '/') {
    path += "/";
  }
  return path;
}

bool isDirectory(const std::string& path) {
  struct stat pathStat;
  if (stat(path.c_str(), &pathStat) != 0) {
    return false;
  }
  return (pathStat.st_mode & S_IFDIR) != 0;
}

unsigned long long computePathSize(const std::string& path) {
  struct stat pathStat;
  if (stat(path.c_str(), &pathStat) != 0) {
    return 0;
  }
  if ((pathStat.st_mode & S_IFDIR) == 0) {
    return static_cast<unsigned long long>(pathStat.st_size);
  }

  unsigned long long totalSize = 0;
  std::vector<std::string> children;
  if (!listDirectory(ensureTrailingSlash(path), children)) {
    return 0;
  }
  for (auto& child: children) {
    totalSize += computePathSize(ensureTrailingSlash(path) + child);
  }
  return totalSize;
}

void printCompressionLine(
  const std::string& label,
  unsigned long long sizeInBytes,
  unsigned long long baselineSizeInBytes
) {
  std::cout << label << ": " << sizeInBytes << " bytes";
  if (baselineSizeInBytes > 0) {
    double ratio = 100.0 * static_cast<double>(sizeInBytes) / static_cast<double>(baselineSizeInBytes);
    std::cout << " (" << std::fixed << std::setprecision(2) << ratio << "% of original PRESS size)";
  }
  std::cout << std::endl;
}

StageResult executeStage(
  ReqRespHelper& reqRespHelper,
  const std::string& label,
  const std::string& payload
) {
  Timer timer;
  timer.reset();
  timer.start();
  reqRespHelper.writeNext(payload);
  picojson::value responseJson = reqRespHelper.explainResponse(reqRespHelper.readNext());
  timer.pause();

  if (!responseJson.contains("Success") || !responseJson.get("Success").get<bool>()) {
    std::cout << "Stage failed: " << label << std::endl;
    exit(EXIT_FAILURE);
  }
  return StageResult(label, timer.getMilliSeconds(), responseJson);
}

void printStageSummary(const std::vector<StageResult>& stageResults) {
  long long totalMilliseconds = 0;
  std::cout << std::endl << "==== Stable Batch Timing Summary (ms) ====" << std::endl;
  for (auto& stageResult: stageResults) {
    std::cout << stageResult.label << ": " << stageResult.milliseconds << std::endl;
    totalMilliseconds += stageResult.milliseconds;
  }
  std::cout << "Total: " << totalMilliseconds << std::endl;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout
      << "Usage: press_core_test_client <request_path> <response_path> "
      << "[--dataset DATASET] [--gps-reader READER] [--gps-file FILE]... [FILE ...]"
      << std::endl;
    return EXIT_FAILURE;
  }

  // Open communication channel.
  ReqRespHelper reqRespHelper(argv[1], argv[2]);
  std::string datasetFolder = kDefaultDatasetFolder;
  const std::string graphReaderType = kDefaultGraphReaderType;
  std::string gpsReaderType = kDefaultTDriveReaderType;
  std::vector<std::string> gpsFiles;

  for (int i = 3; i < argc; ++i) {
    std::string argument = argv[i];
    if (isFlag(argument, "--dataset")) {
      if (i + 1 >= argc) {
        std::cout << "Missing value for --dataset" << std::endl;
        return EXIT_FAILURE;
      }
      datasetFolder = argv[++i];
      gpsReaderType = defaultGPSReaderTypeForDataset(datasetFolder);
      continue;
    }
    if (isFlag(argument, "--gps-reader")) {
      if (i + 1 >= argc) {
        std::cout << "Missing value for --gps-reader" << std::endl;
        return EXIT_FAILURE;
      }
      gpsReaderType = argv[++i];
      continue;
    }
    if (isFlag(argument, "--gps-file")) {
      if (i + 1 >= argc) {
        std::cout << "Missing value for --gps-file" << std::endl;
        return EXIT_FAILURE;
      }
      gpsFiles.emplace_back(argv[++i]);
      continue;
    }
    gpsFiles.emplace_back(argument);
  }

  if (gpsFiles.empty()) {
    gpsFiles.emplace_back(defaultGPSFileForDataset(datasetFolder));
  }

  std::vector<StageResult> stageResults;
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "ReadRoadnetFromDataSource",
    "{\"Cmd\":\"ReadRoadnetFromDataSource\", \"Folder\":\"" + datasetFolder + "\", \"GraphReaderType\":\"" + graphReaderType + "\"}"
  ));
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpRoadnetToBinary",
    "{\"Cmd\":\"DumpRoadnetToBinary\", \"Folder\":\"" + datasetFolder + "\"}"
  ));
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "BuildGridIndex",
    "{\"Cmd\":\"BuildGridIndex\", \"CellWidth\":50, \"CellHeight\":50}"
  ));
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpGridIndexToBinary",
    "{\"Cmd\":\"DumpGridIndexToBinary\", \"Folder\":\"" + datasetFolder + "\"}"
  ));
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "BuildSPTable",
    "{\"Cmd\":\"BuildSPTable\", \"MaxDist\":4000}"
  ));
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpSPTableToBinary",
    "{\"Cmd\":\"DumpSPTableToBinary\", \"Folder\":\"" + datasetFolder + "\"}"
  ));

  for (auto i = 0; i < gpsFiles.size(); ++i) {
    stageResults.emplace_back(executeStage(
      reqRespHelper,
      "AddGPSTrajectoryAndMapMatch[" + std::to_string(i) + "]",
      "{\"Cmd\":\"AddGPSTrajectoryAndMapMatch\", \"FileName\":\"" + gpsFiles.at(i) +
      "\", \"SigmaZ\":4.07, \"MaxGPSBias\":50, \"MaxDistDifference\":2000, \"GPSTrajectoryReaderType\":\"" +
      gpsReaderType + "\"}"
    ));
  }

  // Dump GPS trajectories to binary.
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpGPSTrajectoriesToBinary",
    "{\"Cmd\":\"DumpGPSTrajectoriesToBinary\", \"Folder\":\"" + datasetFolder + "/gps_trajectories\"}"
  ));
  // Dump map matched trajectories to binary.
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpMapMatchedTrajectoriesToBinary",
    "{\"Cmd\":\"DumpMapMatchedTrajectoriesToBinary\", \"Folder\":\"" + datasetFolder + "/map_matched_trajectories\"}"
  ));
  // Reformat GPS trajectories and map matched trajectories as PRESS trajectories.
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "ReformatTrajectories",
    "{\"Cmd\":\"ReformatTrajectories\"}"
  ));
  // Dump PRESS trajectories to binary.
  StageResult pressDumpResult = executeStage(
    reqRespHelper,
    "DumpPRESSTrajectoriesToBinary",
    "{\"Cmd\":\"DumpPRESSTrajectoriesToBinary\", \"Folder\":\"" + datasetFolder + "/press_trajectories\"}"
  );
  stageResults.emplace_back(pressDumpResult);
  // Train
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "TrainACAutomatonHuffmanTreeAndBuildAuxiliary",
    "{\"Cmd\":\"TrainACAutomatonHuffmanTreeAndBuildAuxiliary\", \"Theta\":3}"
  ));
  // Dump AC automaton, huffman tree and auxiliary to binary.
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "DumpACAutomatonHuffmanTreeAndAuxiliaryToBinary",
    "{\"Cmd\":\"DumpACAutomatonHuffmanTreeAndAuxiliaryToBinary\"}"
  ));
  // SP compression
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "SPCompression",
    "{\"Cmd\":\"SPCompression\"}"
  ));
  // Dump SP compression results to binary.
  StageResult spDumpResult = executeStage(
    reqRespHelper,
    "DumpSPCompressionResultsToBinary",
    "{\"Cmd\":\"DumpSPCompressionResultsToBinary\"}"
  );
  stageResults.emplace_back(spDumpResult);
  // FST compression
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "FSTCompression",
    "{\"Cmd\":\"FSTCompression\"}"
  ));
  // Dump FST compression results to binary.
  StageResult fstDumpResult = executeStage(
    reqRespHelper,
    "DumpFSTCompressionResultsToBinary",
    "{\"Cmd\":\"DumpFSTCompressionResultsToBinary\"}"
  );
  stageResults.emplace_back(fstDumpResult);
  // BTC compression
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "BTCCompression",
    "{\"Cmd\":\"BTCCompression\", \"TSND\":10, \"NSTD\":10}"
  ));
  // Dump BTC compression results to binary.
  StageResult btcDumpResult = executeStage(
    reqRespHelper,
    "DumpBTCCompressionResultsToBinary",
    "{\"Cmd\":\"DumpBTCCompressionResultsToBinary\"}"
  );
  stageResults.emplace_back(btcDumpResult);
  // PRESS compression
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "PRESSCompression",
    "{\"Cmd\":\"PRESSCompression\", \"TSND\":10, \"NSTD\":10}"
  ));
  // Dump PRESS compression results to binary.
  StageResult pressCompressionDumpResult = executeStage(
    reqRespHelper,
    "DumpPRESSCompressionResultsToBinary",
    "{\"Cmd\":\"DumpPRESSCompressionResultsToBinary\"}"
  );
  stageResults.emplace_back(pressCompressionDumpResult);
  // PRESS de-compression
  stageResults.emplace_back(executeStage(
    reqRespHelper,
    "PRESSDeCompression",
    "{\"Cmd\":\"PRESSDeCompression\"}"
  ));
  // Dump PRESS de-compression results to binary.
  StageResult pressDecompressionDumpResult = executeStage(
    reqRespHelper,
    "DumpPRESSDeCompressionResultsToBinary",
    "{\"Cmd\":\"DumpPRESSDeCompressionResultsToBinary\"}"
  );
  stageResults.emplace_back(pressDecompressionDumpResult);

  printStageSummary(stageResults);

  std::string pressTrajectoryFolder = extractDumpFolder(pressDumpResult.responseJson);
  std::string spCompressionFolder = extractDumpFolder(spDumpResult.responseJson);
  std::string fstCompressionFolder = extractDumpFolder(fstDumpResult.responseJson);
  std::string btcCompressionFolder = extractDumpFolder(btcDumpResult.responseJson);
  std::string pressCompressionFolder = extractDumpFolder(pressCompressionDumpResult.responseJson);
  std::string pressDecompressionFolder = extractDumpFolder(pressDecompressionDumpResult.responseJson);

  unsigned long long originalPressSize = computePathSize(pressTrajectoryFolder);
  unsigned long long spCompressionSize = computePathSize(spCompressionFolder);
  unsigned long long fstCompressionSize = computePathSize(fstCompressionFolder);
  unsigned long long btcCompressionSize = computePathSize(btcCompressionFolder);
  unsigned long long pressCompressionSize = computePathSize(pressCompressionFolder);
  unsigned long long pressDecompressionSize = computePathSize(pressDecompressionFolder);

  std::cout << std::endl << "==== Stable Batch Size Summary ====" << std::endl;
  std::cout << "Input GPS files: " << gpsFiles.size() << std::endl;
  printCompressionLine("Original PRESS trajectories", originalPressSize, 0);
  printCompressionLine("SP compression", spCompressionSize, originalPressSize);
  printCompressionLine("FST compression", fstCompressionSize, originalPressSize);
  printCompressionLine("BTC compression", btcCompressionSize, originalPressSize);
  printCompressionLine("PRESS compressed", pressCompressionSize, originalPressSize);
  printCompressionLine("PRESS decompression dump", pressDecompressionSize, originalPressSize);

  return EXIT_SUCCESS;
}
