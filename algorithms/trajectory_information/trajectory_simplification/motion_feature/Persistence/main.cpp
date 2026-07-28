#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kScaleFactorPrecision = 1e-4;
constexpr double kDefaultBeta = 10.0 * kPi / 180.0;
constexpr double kDefaultDistanceStart = 2.0;

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

double angleDiff(double a, double b) {
    const double pi2 = 2.0 * kPi;
    double diff = a - b;
    while (diff > kPi) {
        diff -= pi2;
    }
    while (diff <= -kPi) {
        diff += pi2;
    }
    return diff;
}

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
                                   std::vector<double>& y) {
    std::ifstream input(inputPath);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + inputPath);
    }

    std::vector<double> longitude;
    std::vector<double> latitude;
    std::vector<double> t;
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
    return rows;
}

template <typename T>
std::vector<T> rangeInclusiveStep(T from, T to, T step) {
    std::vector<T> values;
    while (from <= to) {
        values.push_back(from);
        from += step;
    }
    return values;
}

template <typename T>
std::vector<T> slice(const std::vector<T>& values, const std::vector<int>& indices) {
    std::vector<T> out;
    out.reserve(indices.size());
    for (int index : indices) {
        out.push_back(values.at(static_cast<std::size_t>(index)));
    }
    return out;
}

void simplifyByIndex(const std::vector<double>& x,
                     const std::vector<double>& y,
                     const std::vector<int>& inIndex,
                     std::vector<int>& outIndex,
                     double minimumDistance) {
    if (x.size() != y.size()) {
        throw std::runtime_error("x/y size mismatch.");
    }

    outIndex.clear();
    if (inIndex.size() < 5) {
        outIndex = inIndex;
        return;
    }

    std::vector<double> sx = slice(x, inIndex);
    std::vector<double> sy = slice(y, inIndex);
    std::vector<double> curvature;
    curvature.reserve(inIndex.size());
    const int pointCount = static_cast<int>(inIndex.size());
    for (int i = 0; i < pointCount - 1; ++i) {
        curvature.push_back(std::atan2(sy[static_cast<std::size_t>(i + 1)] - sy[static_cast<std::size_t>(i)],
                                       sx[static_cast<std::size_t>(i + 1)] - sx[static_cast<std::size_t>(i)]));
    }
    curvature.push_back(0.0);
    for (int i = pointCount - 1; i > 0; --i) {
        curvature[static_cast<std::size_t>(i)] = angleDiff(curvature[static_cast<std::size_t>(i)], curvature[static_cast<std::size_t>(i - 1)]);
    }
    curvature[0] = 0.0;

    std::vector<int> maxima;
    std::vector<int> minima;
    if (curvature[0] > curvature[1]) {
        maxima.push_back(0);
    } else {
        minima.push_back(0);
    }
    for (int i = 1; i < pointCount - 1; ++i) {
        double c1 = curvature[static_cast<std::size_t>(i - 1)];
        double c2 = curvature[static_cast<std::size_t>(i)];
        double c3 = curvature[static_cast<std::size_t>(i + 1)];
        if (c2 >= c1 && c2 > c3) {
            maxima.push_back(i);
        } else if (c2 < c1 && c2 <= c3) {
            minima.push_back(i);
        }
    }
    if (curvature[static_cast<std::size_t>(pointCount - 1)] > curvature[static_cast<std::size_t>(pointCount - 2)]) {
        maxima.push_back(pointCount - 1);
    } else {
        minima.push_back(pointCount - 1);
    }

    std::vector<int> ccLeft;
    std::vector<int> ccRight;
    std::vector<bool> ccTerminated;
    std::vector<bool> ccPaused;
    int numTerminated = 0;
    std::map<int, int> indexToMaxima;
    std::vector<std::vector<int>> ccInMaxima(maxima.size());
    std::set<int> betaPersistence;

    for (int index : minima) {
        ccLeft.push_back(index);
        ccRight.push_back(index);
        ccTerminated.push_back(false);
        ccPaused.push_back(false);
    }
    for (std::size_t i = 0; i < maxima.size(); ++i) {
        indexToMaxima[maxima[i]] = static_cast<int>(i);
    }

    const int ccCount = static_cast<int>(minima.size());
    while (numTerminated < ccCount) {
        for (int i = 0; i < ccCount; ++i) {
            if (ccTerminated[static_cast<std::size_t>(i)] || ccPaused[static_cast<std::size_t>(i)]) {
                continue;
            }

            int a = ccLeft[static_cast<std::size_t>(i)];
            int b = ccRight[static_cast<std::size_t>(i)];
            int expandIndex = -1;
            if (a <= 0 && b >= pointCount - 1) {
                ccTerminated[static_cast<std::size_t>(i)] = true;
                ++numTerminated;
                betaPersistence.insert(minima[static_cast<std::size_t>(i)]);
            } else if (a <= 0 && b < pointCount - 1) {
                expandIndex = b + 1;
                ccRight[static_cast<std::size_t>(i)] = expandIndex;
            } else if (a > 0 && b >= pointCount - 1) {
                expandIndex = a - 1;
                ccLeft[static_cast<std::size_t>(i)] = expandIndex;
            } else {
                if (curvature[static_cast<std::size_t>(a - 1)] <= curvature[static_cast<std::size_t>(b + 1)]) {
                    expandIndex = a - 1;
                    ccLeft[static_cast<std::size_t>(i)] = expandIndex;
                } else {
                    expandIndex = b + 1;
                    ccRight[static_cast<std::size_t>(i)] = expandIndex;
                }
            }

            if (expandIndex >= 0) {
                auto it = indexToMaxima.find(expandIndex);
                if (it != indexToMaxima.end()) {
                    int mp = it->second;
                    ccInMaxima[static_cast<std::size_t>(mp)].push_back(i);
                    int numCC = static_cast<int>(ccInMaxima[static_cast<std::size_t>(mp)].size());
                    if (expandIndex == 0 || expandIndex == pointCount - 1) {
                        // edge
                    } else if (numCC == 2) {
                        int cc1 = ccInMaxima[static_cast<std::size_t>(mp)][0];
                        int cc2 = i;
                        if (minima[static_cast<std::size_t>(cc1)] < minima[static_cast<std::size_t>(cc2)]) {
                            ccPaused[static_cast<std::size_t>(cc1)] = false;
                            ccTerminated[static_cast<std::size_t>(cc2)] = true;
                            ++numTerminated;
                            ccLeft[static_cast<std::size_t>(cc1)] = std::min(ccLeft[static_cast<std::size_t>(cc1)], ccLeft[static_cast<std::size_t>(cc2)]);
                            ccRight[static_cast<std::size_t>(cc1)] = std::max(ccRight[static_cast<std::size_t>(cc1)], ccRight[static_cast<std::size_t>(cc2)]);
                            if (curvature[static_cast<std::size_t>(expandIndex)] - curvature[static_cast<std::size_t>(minima[static_cast<std::size_t>(cc2)])] > kDefaultBeta) {
                                betaPersistence.insert(expandIndex);
                                betaPersistence.insert(minima[static_cast<std::size_t>(cc2)]);
                            }
                        } else {
                            ccTerminated[static_cast<std::size_t>(cc1)] = true;
                            ccPaused[static_cast<std::size_t>(cc2)] = false;
                            ++numTerminated;
                            if (curvature[static_cast<std::size_t>(expandIndex)] - curvature[static_cast<std::size_t>(minima[static_cast<std::size_t>(cc1)])] > kDefaultBeta) {
                                betaPersistence.insert(expandIndex);
                                betaPersistence.insert(minima[static_cast<std::size_t>(cc1)]);
                            }
                            ccLeft[static_cast<std::size_t>(cc2)] = std::min(ccLeft[static_cast<std::size_t>(cc1)], ccLeft[static_cast<std::size_t>(cc2)]);
                            ccRight[static_cast<std::size_t>(cc2)] = std::max(ccRight[static_cast<std::size_t>(cc1)], ccRight[static_cast<std::size_t>(cc2)]);
                        }
                    } else if (numCC > 2) {
                        throw std::runtime_error("#CCs assigned to one maxima should never exceed 2.");
                    } else {
                        ccPaused[static_cast<std::size_t>(i)] = true;
                    }
                }
            }
        }
    }

    std::vector<int> betaList(betaPersistence.begin(), betaPersistence.end());
    bool firstIsMinima = minima.size() > maxima.size();
    bool lastIsMinima = minima.size() > maxima.size();
    int firstIdx = betaList.front();
    int lastIdx = betaList.back();
    int mm = static_cast<int>(std::min(maxima.size(), minima.size()));
    for (int i = 0; i < mm; ++i) {
        if (maxima[static_cast<std::size_t>(i)] == firstIdx) {
            firstIsMinima = false;
            break;
        }
        if (minima[static_cast<std::size_t>(i)] == firstIdx) {
            firstIsMinima = true;
            break;
        }
    }
    for (int i = 0; i < mm; ++i) {
        if (maxima[maxima.size() - 1 - static_cast<std::size_t>(i)] == lastIdx) {
            lastIsMinima = false;
            break;
        }
        if (minima[minima.size() - 1 - static_cast<std::size_t>(i)] == lastIdx) {
            lastIsMinima = true;
            break;
        }
    }

    if (firstIdx != 0) {
        if (firstIsMinima) {
            int extra = 0;
            for (int idx : maxima) {
                if (idx > firstIdx) {
                    break;
                }
                if (curvature[static_cast<std::size_t>(idx)] > curvature[static_cast<std::size_t>(extra)]) {
                    extra = idx;
                }
            }
            if (extra != 0) {
                betaList.insert(betaList.begin(), extra);
            }
            betaList.insert(betaList.begin(), 0);
        } else {
            int extra = 0;
            for (int idx : minima) {
                if (idx > firstIdx) {
                    break;
                }
                if (curvature[static_cast<std::size_t>(idx)] < curvature[static_cast<std::size_t>(extra)]) {
                    extra = idx;
                }
            }
            if (extra != 0) {
                betaList.insert(betaList.begin(), extra);
            }
            betaList.insert(betaList.begin(), 0);
        }
    }

    if (lastIdx != pointCount - 1) {
        if (lastIsMinima) {
            int extra = pointCount - 1;
            for (int i = static_cast<int>(maxima.size()) - 1; i >= 0; --i) {
                int idx = maxima[static_cast<std::size_t>(i)];
                if (idx < lastIdx) {
                    break;
                }
                if (curvature[static_cast<std::size_t>(idx)] > curvature[static_cast<std::size_t>(extra)]) {
                    extra = idx;
                }
            }
            if (extra != pointCount - 1) {
                betaList.push_back(extra);
            }
            betaList.push_back(pointCount - 1);
        } else {
            int extra = pointCount - 1;
            for (int i = static_cast<int>(minima.size()) - 1; i >= 0; --i) {
                int idx = minima[static_cast<std::size_t>(i)];
                if (idx < lastIdx) {
                    break;
                }
                if (curvature[static_cast<std::size_t>(idx)] < curvature[static_cast<std::size_t>(extra)]) {
                    extra = idx;
                }
            }
            if (extra != pointCount - 1) {
                betaList.push_back(extra);
            }
            betaList.push_back(pointCount - 1);
        }
    }

    outIndex.push_back(inIndex.front());
    int currentIdx = 1;
    minimumDistance *= minimumDistance;
    for (std::size_t i = static_cast<std::size_t>(currentIdx + 1); i < betaList.size(); ++i) {
        int nextIdx = betaList[i];
        double distance = std::pow(sy[static_cast<std::size_t>(nextIdx)] - sy[static_cast<std::size_t>(currentIdx)], 2.0) +
                          std::pow(sx[static_cast<std::size_t>(nextIdx)] - sx[static_cast<std::size_t>(currentIdx)], 2.0);
        if (distance < minimumDistance) {
            if (std::abs(curvature[static_cast<std::size_t>(nextIdx)]) > std::abs(curvature[static_cast<std::size_t>(currentIdx)])) {
                currentIdx = nextIdx;
            }
        } else {
            outIndex.push_back(inIndex[static_cast<std::size_t>(currentIdx)]);
            currentIdx = nextIdx;
        }
    }
    if (outIndex.back() != inIndex[static_cast<std::size_t>(betaList.back())]) {
        outIndex.push_back(inIndex[static_cast<std::size_t>(betaList.back())]);
    }
}

void simplifyByIndexCascade(const std::vector<double>& x,
                            const std::vector<double>& y,
                            std::vector<int>& outIndex,
                            double minimumDistance) {
    double startThreshold = std::min(kDefaultDistanceStart, minimumDistance / 8.0);
    int numCascade = static_cast<int>(std::floor(std::log(minimumDistance / startThreshold) / std::log(2.0))) + 1;
    double k = std::pow(minimumDistance / startThreshold, 1.0 / static_cast<double>(numCascade - 1));
    double distance = startThreshold;

    std::vector<int> subIndex = rangeInclusiveStep<int>(0, static_cast<int>(x.size()) - 1, 1);
    for (int i = 0; i < numCascade; ++i) {
        std::vector<int> localOutIndex;
        simplifyByIndex(x, y, subIndex, localOutIndex, distance);
        distance *= k;
        subIndex = std::move(localOutIndex);
    }
    outIndex = std::move(subIndex);
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
            std::cerr << "Usage: Persistence_cpp.exe <geolife_plt_path> <minimum_distance> <output_path>\n";
            return 1;
        }

        const std::string inputPath = argv[1];
        const double minimumDistance = std::stod(argv[2]);
        const std::string outputPath = argv[3];

        std::vector<double> x;
        std::vector<double> y;
        const std::vector<RawPoint> rows = parseGeoLife(inputPath, x, y);
        if (rows.empty()) {
            std::ofstream output(outputPath);
            output << std::fixed << std::setprecision(6) << 0.0 << '\n';
            return 0;
        }

        const auto start = std::chrono::steady_clock::now();
        std::vector<int> indices;
        simplifyByIndexCascade(x, y, indices, minimumDistance);
        const auto end = std::chrono::steady_clock::now();
        const double runtimeSeconds = std::chrono::duration<double>(end - start).count();

        writeOutput(outputPath, rows, indices, runtimeSeconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
