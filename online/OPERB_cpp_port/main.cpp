#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct GPSPoint {
    int index = 0;
    int lineId = 0;
    double longitude = 0.0;
    double latitude = 0.0;
    std::string time;
    std::string rawLine;
};

struct GPSLine {
    int index = 1;
    double length = 0.0;
    double angle = 0.0;
    GPSPoint* startPoint = nullptr;
    GPSPoint* endPoint = nullptr;
};

struct GPSCompLine {
    int index = 1;
    int points = 0;
    int actPoints = 0;

    GPSPoint* startPoint = nullptr;

    GPSPoint* endPointR = nullptr;
    double lengthR = 0.0;
    double angleR = 0.0;

    GPSPoint* endPointRL = nullptr;
    double lengthRL = 0.0;
    double angleRL = 0.0;

    GPSPoint* endPointRF = nullptr;
    double lengthRF = 0.0;
    double angleRF = 0.0;
};

double getDistanceOfP2P(double lat1, double lng1, double lat2, double lng2) {
    const double dlat = lat1 - lat2;
    const double dlng = lng1 - lng2;
    return std::sqrt(dlat * dlat + dlng * dlng);
}

double getIncludedAngle(double angle1, double angle2) {
    double theta = angle2 - angle1;
    if (theta > kPi) {
        theta = -2.0 * kPi + theta;
    }
    if (theta < -kPi) {
        theta = 2.0 * kPi + theta;
    }
    return theta;
}

double getAngleOfVector(double latS, double lngS, double latE, double lngE) {
    if (latS == latE && lngS == lngE) {
        return 0.0;
    }
    if (latS == latE && lngS < lngE) {
        return 0.0;
    }
    if (latS == latE && lngS > lngE) {
        return kPi;
    }
    if (latS < latE && lngS == lngE) {
        return kPi / 2.0;
    }
    if (latS > latE && lngS == lngE) {
        return 3.0 * kPi / 2.0;
    }

    const double high = getDistanceOfP2P(latE, lngE, latS, lngE);
    const double length = getDistanceOfP2P(latS, lngS, latE, lngE);
    if (length == 0.0) {
        return 0.0;
    }

    double angle = std::asin(high / length);
    if (latS < latE && lngS > lngE) {
        angle = kPi - angle;
    }
    if (latS > latE && lngS > lngE) {
        angle = kPi + angle;
    }
    if (latS > latE && lngS < lngE) {
        angle = 2.0 * kPi - angle;
    }
    return angle;
}

double getDistanceOfP2R(const GPSLine& line, const GPSPoint& loc) {
    const double r = getDistanceOfP2P(
        line.startPoint->latitude,
        line.startPoint->longitude,
        loc.latitude,
        loc.longitude);
    if (r == 0.0) {
        return 0.0;
    }

    const double sita = getAngleOfVector(
        line.startPoint->latitude,
        line.startPoint->longitude,
        loc.latitude,
        loc.longitude);
    return r * std::sin(sita - line.angle);
}

double getDistanceOfP2RL(const GPSCompLine& cline, const GPSPoint& loc) {
    GPSLine line;
    line.startPoint = cline.startPoint;
    line.endPoint = cline.endPointRL;
    line.angle = cline.angleRL;
    line.length = cline.lengthRL;
    return getDistanceOfP2R(line, loc);
}

class OperbCompressor {
public:
    explicit OperbCompressor(double threshold) : threshold_(threshold), threshold2_(threshold / 2.0) {}

    void loadTrajectory(const std::string& inputPath) {
        std::ifstream input(inputPath);
        if (!input) {
            throw std::runtime_error("Failed to open input file: " + inputPath);
        }

        std::string line;
        int index = 0;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);
            GPSPoint point;
            if (!(iss >> point.latitude >> point.longitude >> point.time)) {
                continue;
            }

            point.index = index++;
            point.rawLine = line;
            trajectory_.push_back(point);
        }
    }

    std::vector<GPSLine> compress() {
        std::vector<GPSLine> lines;
        if (trajectory_.empty()) {
            return lines;
        }

        std::vector<GPSCompLine> compLines;
        const int iEnd = static_cast<int>(trajectory_.size()) - 1;
        int lineId = 0;
        int n = 0;

        double maxPDis = 0.0;
        double maxNDis = 0.0;

        GPSPoint* loc = &trajectory_[n++];
        GPSCompLine cline;
        cline.index = ++lineId;
        loc->lineId = cline.index;
        cline.startPoint = loc;
        cline.points++;
        compLines.push_back(cline);

        n = getNextValidPoint(n, iEnd, compLines.back());
        maxPDis = 0.0;
        maxNDis = 0.0;

        while (n <= iEnd) {
            loc = &trajectory_[n++];

            const double rn = getDistanceOfP2P(
                loc->latitude,
                loc->longitude,
                compLines.back().startPoint->latitude,
                compLines.back().startPoint->longitude);
            const double alpha = getAngleOfVector(
                compLines.back().startPoint->latitude,
                compLines.back().startPoint->longitude,
                loc->latitude,
                loc->longitude);
            double theta = getIncludedAngle(compLines.back().angleRF, alpha);
            double dis = rn * std::sin(theta);

            if (theta > kPi / 2.0) {
                theta = -kPi + theta;
                dis = -dis;
            } else if (theta < -kPi / 2.0) {
                theta = theta + kPi;
                dis = -dis;
            }

            if (dis < maxNDis) {
                maxNDis = dis;
            }
            if (dis > maxPDis) {
                maxPDis = dis;
            }

            int isDeviase = 0;
            if (maxPDis - maxNDis >= threshold_) {
                isDeviase = 1;
            } else if (std::ceil(rn / threshold2_ - 0.5) == std::ceil(compLines.back().lengthRL / threshold2_ - 0.5) &&
                       std::abs(getDistanceOfP2RL(compLines.back(), *loc)) >= threshold_) {
                isDeviase = 1;
            }

            if (isDeviase > 0) {
                double disL = getDistanceOfP2RL(compLines.back(), *loc);
                double disP2P = getDistanceOfP2P(
                    loc->latitude,
                    loc->longitude,
                    compLines.back().endPointRL->latitude,
                    compLines.back().endPointRL->longitude);

                while (n <= iEnd && std::abs(disL) < threshold_ && disP2P < threshold_) {
                    compLines.back().points++;
                    loc->lineId = compLines.back().index;
                    loc = &trajectory_[n++];
                    disL = getDistanceOfP2RL(compLines.back(), *loc);
                    disP2P = getDistanceOfP2P(
                        loc->latitude,
                        loc->longitude,
                        compLines.back().endPointRL->latitude,
                        compLines.back().endPointRL->longitude);
                }

                GPSCompLine nextLine;
                nextLine.index = ++lineId;
                nextLine.startPoint = compLines.back().endPointRL;
                compLines.push_back(nextLine);

                n = getNextValidPoint(n - 1, iEnd, compLines.back());
                if (n > iEnd) {
                    break;
                }

                maxPDis = 0.0;
                maxNDis = 0.0;
            } else if (rn - compLines.back().lengthRF >= threshold2_ / 2.0) {
                compLines.back().points++;
                compLines.back().actPoints++;
                loc->lineId = compLines.back().index;

                compLines.back().endPointR = loc;
                compLines.back().lengthR = rn;
                compLines.back().angleR = alpha;

                compLines.back().endPointRL = loc;
                compLines.back().lengthRL = rn;
                compLines.back().angleRL = alpha;

                const double oldm = compLines.back().lengthRF / threshold2_;
                double m = rn / threshold2_;
                m = std::ceil(m - 0.5);
                compLines.back().lengthRF = m * threshold2_;

                double maxTheta = 0.0;
                for (double j = oldm + 1.0; j <= m; j += 1.0) {
                    const double ratio = (dis > 0.0) ? (maxPDis / threshold2_ / j) : (maxNDis / threshold2_ / j);
                    const double safeRatio = std::max(-1.0, std::min(1.0, ratio));
                    maxTheta += std::asin(safeRatio) / j;
                    if (std::abs(maxTheta) > std::abs(theta)) {
                        maxTheta = theta;
                        break;
                    }
                }
                compLines.back().angleRF += maxTheta;
                compLines.back().angleRF = std::fmod(compLines.back().angleRF + 2.0 * kPi, 2.0 * kPi);
            } else {
                compLines.back().points++;
                loc->lineId = compLines.back().index;
                compLines.back().endPointR = loc;
                compLines.back().lengthR = rn;
                compLines.back().angleR = alpha;
            }
        }

        for (const auto& compLine : compLines) {
            if (compLine.startPoint == nullptr || compLine.endPointRL == nullptr) {
                continue;
            }
            GPSLine line;
            line.index = compLine.index;
            line.startPoint = compLine.startPoint;
            line.endPoint = compLine.endPointRL;
            line.angle = compLine.angleRL;
            line.length = compLine.lengthRL;
            lines.push_back(line);
        }

        return lines;
    }

    const std::vector<GPSPoint>& trajectory() const { return trajectory_; }

private:
    int getNextValidPoint(int n, int iEnd, GPSCompLine& cline) {
        while (n <= iEnd) {
            GPSPoint* loc = &trajectory_[n++];
            loc->lineId = cline.index;
            cline.points++;

            cline.endPointR = loc;
            cline.angleR = getAngleOfVector(
                cline.startPoint->latitude,
                cline.startPoint->longitude,
                loc->latitude,
                loc->longitude);
            const double r1 = getDistanceOfP2P(
                loc->latitude,
                loc->longitude,
                cline.startPoint->latitude,
                cline.startPoint->longitude);
            cline.lengthR = r1;

            if (std::abs(r1) >= threshold_) {
                cline.actPoints++;
                cline.endPointRL = loc;
                cline.angleRL = cline.angleR;
                cline.lengthRL = r1;

                cline.angleRF = cline.angleR;
                double m = r1 / threshold2_;
                m = std::ceil(m - 0.5);
                cline.lengthRF = m * threshold2_;
                break;
            }
        }
        return n;
    }

    double threshold_;
    double threshold2_;
    std::vector<GPSPoint> trajectory_;
};

std::vector<const GPSPoint*> buildOutputPoints(const std::vector<GPSLine>& lines) {
    std::vector<const GPSPoint*> output;
    output.reserve(lines.size() + 1);
    for (const auto& line : lines) {
        output.push_back(line.startPoint);
    }
    if (!lines.empty()) {
        output.push_back(lines.back().endPoint);
    }
    return output;
}

void writeOutput(const std::string& outputPath, const std::vector<const GPSPoint*>& points, double runtimeSeconds) {
    std::ofstream output(outputPath);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + outputPath);
    }

    int lastIndex = -1;
    for (const auto* point : points) {
        if (point == nullptr || point->index == lastIndex) {
            continue;
        }
        output << point->rawLine << '\n';
        lastIndex = point->index;
    }
    output << std::fixed << std::setprecision(6) << runtimeSeconds << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: OPERB_cpp.exe <input_path> <epsilon> <output_path>\n";
            return 1;
        }

        const std::string inputPath = argv[1];
        const double epsilon = std::stod(argv[2]);
        const std::string outputPath = argv[3];

        OperbCompressor compressor(epsilon);
        compressor.loadTrajectory(inputPath);
        if (compressor.trajectory().empty()) {
            std::ofstream output(outputPath);
            output << std::fixed << std::setprecision(6) << 0.0 << '\n';
            return 0;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto lines = compressor.compress();
        const auto end = std::chrono::steady_clock::now();
        const double runtimeSeconds = std::chrono::duration<double>(end - start).count();

        const auto outputPoints = buildOutputPoints(lines);
        writeOutput(outputPath, outputPoints, runtimeSeconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
