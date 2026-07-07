#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.1415926;

struct Point {
    double x = 0.0;
    double y = 0.0;
    std::string time;
    int num = 0;
    std::string rawLine;
};

struct Range {
    double left = 0.0;
    double right = 0.0;
};

double standardization(double a) {
    if (a < 0.0) {
        a += 2.0 * kPi;
    }
    if (a >= 2.0 * kPi) {
        a -= 2.0 * kPi;
    }
    return a;
}

Range fan(double d, double error) {
    Range range;
    range.left = standardization(d + error);
    range.right = standardization(d - error);
    return range;
}

bool notInt(const Range& comSub, const Range& range) {
    if (comSub.left < comSub.right) {
        if (range.left > range.right) {
            if (range.right > comSub.left && range.left < comSub.right) {
                return true;
            }
        }
    } else {
        if (range.left < range.right) {
            if (comSub.right > range.left && comSub.left < range.right) {
                return true;
            }
        } else if (range.left < comSub.right || range.right > comSub.left) {
            return true;
        }
    }
    return false;
}

bool isInt1(const Range& inter, double direction) {
    if (inter.left < inter.right) {
        if ((direction <= inter.left) || (direction >= inter.right)) {
            return true;
        }
    } else {
        if ((direction <= inter.left) && (direction >= inter.right)) {
            return true;
        }
    }
    return false;
}

Range intersection(const Range& r1, const Range& r2) {
    Range inter;
    if (r1.left < r1.right) {
        if ((r2.left <= r1.left) || (r2.left >= r1.right)) {
            inter.left = r2.left;
            inter.right = r1.right;
        } else if ((r2.right <= r1.left) || (r2.right >= r1.right)) {
            inter.left = r1.left;
            inter.right = r2.right;
        } else {
            inter.left = r1.left;
            inter.right = r1.right;
        }
    } else {
        if ((r2.left <= r1.left) && (r2.left >= r1.right)) {
            inter.left = r2.left;
            inter.right = r1.right;
        } else if ((r2.right <= r1.left) && (r2.right >= r1.right)) {
            inter.left = r1.left;
            inter.right = r2.right;
        } else {
            inter.left = r1.left;
            inter.right = r1.right;
        }
    }
    return inter;
}

std::vector<Point> readPoints(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + path);
    }

    std::vector<Point> points;
    std::string line;
    int index = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        Point point;
        if (!(iss >> point.x >> point.y >> point.time)) {
            continue;
        }
        point.num = index++;
        point.rawLine = line;
        points.push_back(point);
    }
    return points;
}

std::vector<Point> simplifyInterval(const std::vector<Point>& points, double error) {
    if (points.size() <= 2) {
        return points;
    }

    std::vector<Point> result;
    result.push_back(points[0]);

    double direction12 = standardization(std::atan2(points[1].y - points[0].y, points[1].x - points[0].x));
    Range comSub = fan(direction12, error);
    int s = 0;

    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        double directi = standardization(std::atan2(points[i + 1].y - points[i].y, points[i + 1].x - points[i].x));
        Range interval = fan(directi, error);
        if (notInt(comSub, interval)) {
            result.push_back(points[i]);
            comSub = interval;
            s = static_cast<int>(i);
        } else {
            comSub = intersection(comSub, interval);
            double directsum = standardization(std::atan2(points[i + 1].y - points[s].y, points[i + 1].x - points[s].x));
            if (!isInt1(comSub, directsum)) {
                result.push_back(points[i]);
                comSub = interval;
                s = static_cast<int>(i);
            }
        }
    }

    result.push_back(points.back());
    return result;
}

void writeOutput(const std::string& outputPath, const std::vector<Point>& points, double runtimeSeconds) {
    std::ofstream output(outputPath);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + outputPath);
    }

    for (const auto& point : points) {
        output << point.rawLine << '\n';
    }
    output << std::fixed << std::setprecision(6) << runtimeSeconds << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: Interval_cpp.exe <input_path> <error> <output_path>\n";
            return 1;
        }

        const std::string inputPath = argv[1];
        const double error = std::stod(argv[2]);
        const std::string outputPath = argv[3];

        const std::vector<Point> points = readPoints(inputPath);
        if (points.empty()) {
            std::ofstream output(outputPath);
            output << std::fixed << std::setprecision(6) << 0.0 << '\n';
            return 0;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<Point> simplified = simplifyInterval(points, error);
        const auto end = std::chrono::steady_clock::now();
        const double runtimeSeconds = std::chrono::duration<double>(end - start).count();

        writeOutput(outputPath, simplified, runtimeSeconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
