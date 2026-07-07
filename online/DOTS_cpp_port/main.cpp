#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kScaleFactorPrecision = 1e-4;

struct RawPoint {
    double lat = 0.0;
    double lon = 0.0;
    double t = 0.0;
    std::string rawLine;
};

double limitVal(double val, double lb, double ub) {
    if (val < lb) {
        val = lb;
    }
    if (val > ub) {
        val = ub;
    }
    return val;
}

class DotsSimplifier {
public:
    DotsSimplifier() = default;

    void setParameters(double lssdTh, double k = 2.0, int maxVkSize = 1000000) {
        lssdTh_ = lssdTh;
        lssdUpperBound_ = lssdTh * k;
        maxVkSize_ = maxVkSize;
    }

    void resetInternalData() {
        ptx_.clear();
        pty_.clear();
        ptt_.clear();
        ptIndex_.clear();
        xSum_.clear();
        ySum_.clear();
        tSum_.clear();
        x2Sum_.clear();
        y2Sum_.clear();
        t2Sum_.clear();
        xtSum_.clear();
        ytSum_.clear();
        vK_.clear();
        vL_.clear();
        terminated_.clear();
        numTerminated_ = 0;
        pathK_.clear();
        issed_.clear();
        parents_.clear();
        simplifiedIndex_.clear();
        inputCount_ = 0;
        outputCount_ = 0;
        finished_ = false;
    }

    void feedData(double x, double y, double t) {
        if (finished_) {
            throw std::runtime_error("Feeding data after finish() is not allowed.");
        }

        ptx_.push_back(x);
        pty_.push_back(y);
        ptt_.push_back(t);
        ptIndex_.push_back(static_cast<int>(ptIndex_.size()));

        if (ptIndex_.size() == 1) {
            if (std::abs(t) > 3600.0 * 24.0 * 365.0) {
                throw std::runtime_error("The first timestamp is too large for faithful DOTS normalization.");
            }
            if (std::abs(x) > 2000.0 * 1000.0 || std::abs(y) > 2000.0 * 1000.0) {
                throw std::runtime_error("The first position is too large for faithful DOTS normalization.");
            }
            xSum_.push_back(x);
            ySum_.push_back(y);
            tSum_.push_back(t);
            x2Sum_.push_back(x * x);
            y2Sum_.push_back(y * y);
            t2Sum_.push_back(t * t);
            xtSum_.push_back(x * t);
            ytSum_.push_back(y * t);

            vK_.push_back(0);
            terminated_.push_back(false);
            numTerminated_ = 0;

            pathK_.push_back(std::vector<int>{0});

            inputCount_ = 1;
            outputCount_ = 0;
            simplifiedIndex_.push_back(0);
        } else {
            const std::size_t last = xSum_.size() - 1;
            xSum_.push_back(xSum_[last] + x);
            ySum_.push_back(ySum_[last] + y);
            tSum_.push_back(tSum_[last] + t);
            x2Sum_.push_back(x2Sum_[last] + x * x);
            y2Sum_.push_back(y2Sum_[last] + y * y);
            t2Sum_.push_back(t2Sum_[last] + t * t);
            xtSum_.push_back(xtSum_[last] + x * t);
            ytSum_.push_back(ytSum_[last] + y * t);
        }

        issed_.push_back(0.0);
        parents_.push_back(-1);
    }

    bool readOutputIndex(int& index) {
        if (!finished_ && outputCount_ >= static_cast<int>(simplifiedIndex_.size())) {
            directedAcyclicGraphSearch();
        }

        if (outputCount_ < static_cast<int>(simplifiedIndex_.size())) {
            index = ptIndex_.at(static_cast<std::size_t>(simplifiedIndex_[outputCount_]));
            ++outputCount_;
            return true;
        }
        return false;
    }

    void finish() {
        if (!finished_) {
            finished_ = true;
            directedAcyclicGraphSearch();
        }
    }

private:
    double getLSSD(int fst, int lst) const {
        fst = ptIndex_.at(static_cast<std::size_t>(fst));
        lst = ptIndex_.at(static_cast<std::size_t>(lst));
        if (fst + 1 >= lst) {
            return 0.0;
        }

        const int plst = lst - 1;
        const double c1x = ptx_[fst] * ptt_[lst] - ptx_[lst] * ptt_[fst];
        const double c2x = c1x * c1x;
        const double c3x = ptt_[lst] - ptt_[fst];
        const double c4x = c3x * c3x;
        const double c5x = ptx_[lst] - ptx_[fst];
        const double c6x = c5x * c5x;

        const double c1y = pty_[fst] * ptt_[lst] - pty_[lst] * ptt_[fst];
        const double c2y = c1y * c1y;
        const double c3y = c3x;
        const double c4y = c3y * c3y;
        const double c5y = pty_[lst] - pty_[fst];
        const double c6y = c5y * c5y;

        return (plst - fst) * c2x / c4x
             + c6x / c4x * (t2Sum_[plst] - t2Sum_[fst])
             + (x2Sum_[plst] - x2Sum_[fst])
             + 2.0 * c1x * c5x / c4x * (tSum_[plst] - tSum_[fst])
             - 2.0 * c1x / c3x * (xSum_[plst] - xSum_[fst])
             - 2.0 * c5x / c3x * (xtSum_[plst] - xtSum_[fst])
             + (plst - fst) * c2y / c4y
             + c6y / c4y * (t2Sum_[plst] - t2Sum_[fst])
             + (y2Sum_[plst] - y2Sum_[fst])
             + 2.0 * c1y * c5y / c4y * (tSum_[plst] - tSum_[fst])
             - 2.0 * c1y / c3y * (ySum_[plst] - ySum_[fst])
             - 2.0 * c5y / c3y * (ytSum_[plst] - ytSum_[fst]);
    }

    bool needUpdateVK() const {
        return static_cast<int>(vK_.size()) == numTerminated_ || static_cast<int>(vL_.size()) >= maxVkSize_;
    }

    void updateVK() {
        std::vector<std::vector<int>> newPath;
        newPath.reserve(vL_.size());
        for (std::size_t k = 0; k < vL_.size(); ++k) {
            int indexK = -1;
            const int parentPos = parents_[static_cast<std::size_t>(vL_[k])];
            for (std::size_t m = 0; m < vK_.size(); ++m) {
                if (vK_[m] == parentPos) {
                    indexK = static_cast<int>(m);
                    break;
                }
            }
            if (indexK < 0) {
                throw std::runtime_error("DOTS path reconstruction failed.");
            }
            auto p = pathK_[static_cast<std::size_t>(indexK)];
            p.push_back(vL_[k]);
            newPath.push_back(std::move(p));
        }
        pathK_ = std::move(newPath);

        vK_ = vL_;
        terminated_.assign(vK_.size(), false);
        numTerminated_ = 0;
        vL_.clear();
    }

    void minimizeISSED() {
        for (int i : vL_) {
            double minDistance = issed_[static_cast<std::size_t>(i)];
            int minParent = parents_[static_cast<std::size_t>(i)];
            for (int j : vK_) {
                const double localDistance = getLSSD(j, i);
                const double distance = issed_[static_cast<std::size_t>(j)] + localDistance;
                if (localDistance < lssdTh_ && distance < minDistance) {
                    minDistance = distance;
                    minParent = j;
                }
            }
            issed_[static_cast<std::size_t>(i)] = minDistance;
            parents_[static_cast<std::size_t>(i)] = minParent;
        }
    }

    void viterbiDecode() {
        if (pathK_.empty()) {
            return;
        }
        int inputLayer = static_cast<int>(pathK_[0].size());
        int startLayer = static_cast<int>(simplifiedIndex_.size());
        while (startLayer < inputLayer) {
            bool equal = true;
            int reference = pathK_[0][static_cast<std::size_t>(startLayer)];
            for (const auto& path : pathK_) {
                if (path[static_cast<std::size_t>(startLayer)] != reference) {
                    equal = false;
                    break;
                }
            }
            if (!equal) {
                break;
            }
            simplifiedIndex_.push_back(reference);
            ++startLayer;
        }
    }

    void directedAcyclicGraphSearch() {
        bool vKUpdated = false;
        const int numPoints = static_cast<int>(ptIndex_.size());

        if (finished_) {
            while (true) {
                if (inputCount_ >= numPoints) {
                    break;
                }

                vKUpdated = false;
                for (int i = inputCount_; i < numPoints; ++i) {
                    if (parents_[static_cast<std::size_t>(i)] < 0) {
                        for (std::size_t j = 0; j < vK_.size(); ++j) {
                            const int jIndex = vK_[j];
                            if (!terminated_[j]) {
                                double distance = getLSSD(jIndex, i);
                                if (distance < lssdTh_) {
                                    vL_.push_back(i);
                                    issed_[static_cast<std::size_t>(i)] = issed_[static_cast<std::size_t>(jIndex)] + distance;
                                    parents_[static_cast<std::size_t>(i)] = jIndex;

                                    if (needUpdateVK()) {
                                        minimizeISSED();
                                        updateVK();
                                        viterbiDecode();
                                        vKUpdated = true;
                                    }
                                    break;
                                }
                                if (distance > lssdUpperBound_) {
                                    terminated_[j] = true;
                                    ++numTerminated_;
                                    if (needUpdateVK()) {
                                        minimizeISSED();
                                        updateVK();
                                        viterbiDecode();
                                        vKUpdated = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (parents_[static_cast<std::size_t>(i)] >= 0 && inputCount_ == i) {
                        ++inputCount_;
                    }
                    if (vKUpdated) {
                        break;
                    }
                }

                if (!vKUpdated) {
                    break;
                }
            }
        } else {
            while (true) {
                if (inputCount_ >= numPoints) {
                    break;
                }

                vKUpdated = false;
                for (int i = inputCount_; i < numPoints; ++i) {
                    if (parents_[static_cast<std::size_t>(i)] < 0) {
                        for (std::size_t j = 0; j < vK_.size(); ++j) {
                            const int jIndex = vK_[j];
                            if (!terminated_[j]) {
                                double distance = getLSSD(jIndex, i);
                                if (distance < lssdTh_) {
                                    vL_.push_back(i);
                                    issed_[static_cast<std::size_t>(i)] = issed_[static_cast<std::size_t>(jIndex)] + distance;
                                    parents_[static_cast<std::size_t>(i)] = jIndex;

                                    if (needUpdateVK()) {
                                        minimizeISSED();
                                        updateVK();
                                        viterbiDecode();
                                        vKUpdated = true;
                                    }
                                    break;
                                }
                                if (distance > lssdUpperBound_) {
                                    terminated_[j] = true;
                                    ++numTerminated_;

                                    if (needUpdateVK()) {
                                        minimizeISSED();
                                        updateVK();
                                        viterbiDecode();
                                        vKUpdated = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (parents_[static_cast<std::size_t>(i)] >= 0 && inputCount_ == i) {
                        ++inputCount_;
                    }

                    if (vKUpdated) {
                        break;
                    }
                }

                if (!vKUpdated) {
                    break;
                }
            }
        }
    }

    double lssdTh_ = 10000.0;
    double lssdUpperBound_ = 20000.0;
    int maxVkSize_ = 1000000;

    std::vector<double> ptx_;
    std::vector<double> pty_;
    std::vector<double> ptt_;
    std::vector<int> ptIndex_;

    std::vector<double> xSum_;
    std::vector<double> ySum_;
    std::vector<double> tSum_;
    std::vector<double> x2Sum_;
    std::vector<double> y2Sum_;
    std::vector<double> t2Sum_;
    std::vector<double> xtSum_;
    std::vector<double> ytSum_;
    std::vector<int> vK_;
    std::vector<int> vL_;
    std::vector<bool> terminated_;
    int numTerminated_ = 0;
    std::vector<std::vector<int>> pathK_;
    std::vector<double> issed_;
    std::vector<int> parents_;

    std::vector<int> simplifiedIndex_;
    int inputCount_ = 0;
    int outputCount_ = 0;
    bool finished_ = false;
};

void normalizeData(std::vector<double>& values, bool byMean) {
    if (values.empty()) {
        return;
    }
    double cal = 0.0;
    if (byMean) {
        for (double value : values) {
            cal += value;
        }
        cal /= static_cast<double>(values.size());
    } else {
        cal = values.front();
    }
    for (double& value : values) {
        value -= cal;
    }
}

void mercatorProject(const std::vector<double>& longitude, const std::vector<double>& latitude,
                     std::vector<double>& x, std::vector<double>& y) {
    if (longitude.size() != latitude.size()) {
        throw std::runtime_error("Longitude/latitude size mismatch.");
    }

    x.clear();
    y.clear();
    const double mercatorLb = 2.5 * 2.0 - kPi / 2.0;
    const double mercatorUb = 87.5 * 2.0 - kPi / 2.0;
    const int pointCount = static_cast<int>(longitude.size());
    if (pointCount <= 0) {
        return;
    }

    const double earthRadius = 6378100.0;
    double sf = 0.0;
    for (double lat : latitude) {
        sf += 1.0 / std::cos(lat * kPi / 180.0);
    }
    sf /= static_cast<double>(pointCount);
    sf = std::round(sf / kScaleFactorPrecision) * kScaleFactorPrecision;

    for (int i = 0; i < pointCount; ++i) {
        const double rx = longitude[static_cast<std::size_t>(i)] * kPi / 180.0;
        double ryDeg = limitVal(latitude[static_cast<std::size_t>(i)], mercatorLb, mercatorUb);
        double ry = ryDeg * kPi / 180.0;
        ry = std::log(std::abs(std::tan(ry) + 1.0 / std::cos(ry)));

        x.push_back(rx * earthRadius / sf);
        y.push_back(ry * earthRadius / sf);
    }
}

std::vector<RawPoint> parseGeoLife(const std::string& inputPath,
                                   std::vector<double>& x,
                                   std::vector<double>& y,
                                   std::vector<double>& t) {
    std::ifstream input(inputPath);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + inputPath);
    }

    std::vector<double> longitude;
    std::vector<double> latitude;
    std::vector<RawPoint> rows;
    std::string line;
    int numLine = 0;
    while (std::getline(input, line)) {
        ++numLine;
        if (numLine <= 6 || line.empty()) {
            continue;
        }

        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, ',')) {
            parts.push_back(part);
        }
        if (parts.size() != 7) {
            throw std::runtime_error("Malformed GeoLife line found.");
        }

        const double timestamp = std::stod(parts[4]);
        if (!t.empty() && timestamp - t.back() < 1e-15) {
            continue;
        }

        RawPoint row;
        row.lat = std::stod(parts[0]);
        row.lon = std::stod(parts[1]);
        row.t = timestamp;
        row.rawLine = line;
        rows.push_back(row);

        latitude.push_back(row.lat);
        longitude.push_back(row.lon);
        t.push_back(timestamp);
    }

    mercatorProject(longitude, latitude, x, y);
    normalizeData(x, true);
    normalizeData(y, true);
    normalizeData(t, false);
    return rows;
}

std::vector<int> batchDotsByIndex(const std::vector<double>& x,
                                  const std::vector<double>& y,
                                  const std::vector<double>& t,
                                  double lssdThreshold) {
    DotsSimplifier simplifier;
    simplifier.setParameters(lssdThreshold);
    std::vector<int> simplifiedIndex;
    int idx = -1;

    for (std::size_t i = 0; i < x.size(); ++i) {
        simplifier.feedData(x[i], y[i], t[i]);
        if (simplifier.readOutputIndex(idx)) {
            simplifiedIndex.push_back(idx);
        }
    }
    simplifier.finish();
    while (simplifier.readOutputIndex(idx)) {
        simplifiedIndex.push_back(idx);
    }
    return simplifiedIndex;
}

void writeOutput(const std::string& outputPath,
                 const std::vector<RawPoint>& rows,
                 const std::vector<int>& indices,
                 double runtimeSeconds) {
    std::ofstream output(outputPath);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + outputPath);
    }

    int lastIndex = -1;
    for (int index : indices) {
        if (index == lastIndex || index < 0 || index >= static_cast<int>(rows.size())) {
            continue;
        }
        output << rows[static_cast<std::size_t>(index)].rawLine << '\n';
        lastIndex = index;
    }
    output << std::fixed << std::setprecision(6) << runtimeSeconds << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: DOTS_cpp.exe <geolife_plt_path> <lssd_threshold> <output_path>\n";
            return 1;
        }

        const std::string inputPath = argv[1];
        const double lssdThreshold = std::stod(argv[2]);
        const std::string outputPath = argv[3];

        std::vector<double> x;
        std::vector<double> y;
        std::vector<double> t;
        const std::vector<RawPoint> rows = parseGeoLife(inputPath, x, y, t);
        if (rows.empty()) {
            std::ofstream output(outputPath);
            output << std::fixed << std::setprecision(6) << 0.0 << '\n';
            return 0;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<int> indices = batchDotsByIndex(x, y, t, lssdThreshold);
        const auto end = std::chrono::steady_clock::now();
        const double runtimeSeconds = std::chrono::duration<double>(end - start).count();

        writeOutput(outputPath, rows, indices, runtimeSeconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
