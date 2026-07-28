#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// C++ implementation of CASC (Zhong et al., 2022).  Input is whitespace
// separated: latitude longitude timestamp [optional extra columns].  The
// output retains the original input lines and appends the elapsed seconds,
// following the convention used by the other online programs in this repo.
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusMetres = 6371008.8;
constexpr double kSpeedEpsilon = 1e-12;

struct Point {
    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double timestamp = 0.0;
    std::string rawLine;
};

double normalizeRadians(double radians) {
    radians = std::fmod(radians, 2.0 * kPi);
    return radians < 0.0 ? radians + 2.0 * kPi : radians;
}

double circularDifferenceRadians(double first, double second) {
    const double difference = std::fabs(normalizeRadians(first) - normalizeRadians(second));
    return difference > kPi ? 2.0 * kPi - difference : difference;
}

double haversineMetres(const Point& first, const Point& second) {
    const double lat1 = first.latitudeDeg * kPi / 180.0;
    const double lat2 = second.latitudeDeg * kPi / 180.0;
    const double dLat = lat2 - lat1;
    const double dLon = (second.longitudeDeg - first.longitudeDeg) * kPi / 180.0;
    const double a = std::pow(std::sin(dLat / 2.0), 2.0) +
                     std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(dLon / 2.0), 2.0);
    return 2.0 * kEarthRadiusMetres * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
}

// Initial bearing from first to second, clockwise from true north, in radians.
double bearingRadians(const Point& first, const Point& second) {
    const double lat1 = first.latitudeDeg * kPi / 180.0;
    const double lat2 = second.latitudeDeg * kPi / 180.0;
    const double dLon = (second.longitudeDeg - first.longitudeDeg) * kPi / 180.0;
    const double y = std::sin(dLon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    return normalizeRadians(std::atan2(y, x));
}

double speedMetresPerSecond(const Point& first, const Point& second) {
    const double interval = second.timestamp - first.timestamp;
    if (interval <= 0.0) {
        throw std::runtime_error("Timestamps must be strictly increasing within each trajectory.");
    }
    return haversineMetres(first, second) / interval;
}

// Equation (5) divides by the preceding speed.  Treat a transition to or
// from a stop as important instead of risking division by zero.
double relativeSpeedDifference(double precedingSpeed, double followingSpeed) {
    if (precedingSpeed <= kSpeedEpsilon) {
        return followingSpeed <= kSpeedEpsilon ? 0.0 : INFINITY;
    }
    return std::fabs(followingSpeed - precedingSpeed) / precedingSpeed;
}

std::vector<Point> readPoints(const std::string& inputPath) {
    std::ifstream input(inputPath);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + inputPath);
    }

    std::vector<Point> points;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        std::istringstream row(line);
        Point point;
        if (!(row >> point.latitudeDeg >> point.longitudeDeg >> point.timestamp)) {
            throw std::runtime_error("Expected 'latitude longitude timestamp' at input line " +
                                     std::to_string(lineNumber) + ".");
        }
        if (point.latitudeDeg < -90.0 || point.latitudeDeg > 90.0 ||
            point.longitudeDeg < -180.0 || point.longitudeDeg > 180.0) {
            throw std::runtime_error("Latitude/longitude out of range at input line " +
                                     std::to_string(lineNumber) + ".");
        }
        point.rawLine = line;
        points.push_back(std::move(point));
    }
    return points;
}

std::vector<Point> simplifyCasc(const std::vector<Point>& points,
                                double azimuthThresholdRadians,
                                double speedThresholdRatio,
                                double maxIntervalSeconds) {
    if (points.size() <= 2) {
        return points;
    }

    std::vector<Point> compressed;
    compressed.reserve(points.size());
    compressed.push_back(points.front());

    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const Point& previous = points[i - 1];
        const Point& current = points[i];
        const Point& next = points[i + 1];
        const double intervalBefore = current.timestamp - previous.timestamp;
        const double intervalAfter = next.timestamp - current.timestamp;
        const double turn = circularDifferenceRadians(bearingRadians(previous, current),
                                                       bearingRadians(current, next));
        const double speedDifference = relativeSpeedDifference(speedMetresPerSecond(previous, current),
                                                                speedMetresPerSecond(current, next));

        // CASC removes a point only if all three spatio-temporal tests say it
        // is unimportant.  Hence either long adjacent interval preserves both
        // endpoints of that interval, as specified in the paper.
        const bool unimportant = turn < azimuthThresholdRadians &&
                                 speedDifference < speedThresholdRatio &&
                                 intervalBefore < maxIntervalSeconds &&
                                 intervalAfter < maxIntervalSeconds;
        if (!unimportant) {
            compressed.push_back(current);
        }
    }
    compressed.push_back(points.back());
    return compressed;
}

void writeOutput(const std::string& outputPath, const std::vector<Point>& points, double elapsedSeconds) {
    std::ofstream output(outputPath);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + outputPath);
    }
    for (const Point& point : points) {
        output << point.rawLine << '\n';
    }
    output << std::fixed << std::setprecision(6) << elapsedSeconds << '\n';
}

void printUsage() {
    std::cerr << "Usage: CASC_cpp.exe <input_path> <azimuth_radians> <speed_ratio> "
                 "<max_interval_seconds> <output_path>\n"
                 "  Input rows: latitude longitude timestamp [optional extra columns]\n"
                 "  Typical GPS settings from the paper: 0.55850536 1.0 10 (32 degrees)\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 6) {
            printUsage();
            return argc == 1 ? 0 : 1;
        }
        const double azimuthThresholdRadians = std::stod(argv[2]);
        const double speedThresholdRatio = std::stod(argv[3]);
        const double maxIntervalSeconds = std::stod(argv[4]);
        if (azimuthThresholdRadians < 0.0 || azimuthThresholdRadians > kPi ||
            speedThresholdRatio < 0.0 || maxIntervalSeconds <= 0.0) {
            throw std::runtime_error("Thresholds must satisfy: 0 <= azimuth <= pi radians, speed >= 0, interval > 0.");
        }

        const std::vector<Point> points = readPoints(argv[1]);
        const auto start = std::chrono::steady_clock::now();
        const std::vector<Point> compressed = simplifyCasc(
            points, azimuthThresholdRadians, speedThresholdRatio, maxIntervalSeconds);
        const auto end = std::chrono::steady_clock::now();
        writeOutput(argv[5], compressed, std::chrono::duration<double>(end - start).count());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CASC error: " << error.what() << '\n';
        return 1;
    }
}
