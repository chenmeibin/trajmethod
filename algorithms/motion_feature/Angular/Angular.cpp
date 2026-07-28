#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.1415926;  // Kept identical to the Java source.

struct Point {
    double x;
    double y;
    std::string time;
};

double angularDeviation(const std::vector<Point>& points, std::size_t i) {
    const double d1 = std::atan2(points[i].y - points[i - 1].y,
                                 points[i].x - points[i - 1].x);
    const double d2 = std::atan2(points[i + 1].y - points[i].y,
                                 points[i + 1].x - points[i].x);
    const double deviation = std::min(std::abs(d2 - d1), kPi * 2.0 - std::abs(d2 - d1));

    if (d1 >= 0.0) {
        if (d2 >= d1 + kPi || d2 <= d1) {
            return -deviation;
        }
    } else if (d2 > d1 - kPi && d2 < d1) {
        return -deviation;
    }
    return deviation;
}

std::vector<Point> readTrajectory(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open input trajectory: " + path);
    }

    std::vector<Point> points;
    Point point;
    while (input >> point.x >> point.y >> point.time) {
        points.push_back(point);
    }
    if (!input.eof()) {
        throw std::runtime_error("Malformed trajectory line in: " + path);
    }
    if (points.size() < 2) {
        throw std::runtime_error("The input trajectory must contain at least two points.");
    }
    return points;
}

std::vector<Point> compress(const std::vector<Point>& points, double errorThreshold) {
    std::vector<Point> retained;
    retained.reserve(points.size());
    retained.push_back(points.front());

    double deviation = 0.0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        deviation += angularDeviation(points, i);
        if (std::abs(deviation) > errorThreshold) {  // Same strict test as Java.
            retained.push_back(points[i]);
            deviation = 0.0;
        }
    }
    retained.push_back(points.back());
    return retained;
}

void writeTrajectory(const std::vector<Point>& points, const std::string& path, double elapsedMs) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Cannot open output file: " + path);
    }
    output << std::setprecision(15);
    for (const Point& point : points) {
        output << point.x << ' ' << point.y << ' ' << point.time << '\n';
    }
    output << std::fixed << std::setprecision(6) << (elapsedMs / 1000.0) << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <trajectory.plt> <angle_threshold_radians> [output.plt]\n";
        return 1;
    }

    try {
        const std::string inputPath = argv[1];
        const double errorThreshold = std::stod(argv[2]);
        const std::vector<Point> points = readTrajectory(inputPath);

        const auto start = std::chrono::steady_clock::now();
        const std::vector<Point> retained = compress(points, errorThreshold);
        const auto end = std::chrono::steady_clock::now();

        const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
        const double retentionRate = static_cast<double>(retained.size()) / points.size();

        std::cout << inputPath << '\n';
        std::cout << "running time:" << elapsedMs << '\n';
        // Retained-point ratio, intentionally matching the Java program's label and value.
        std::cout << "compression rate:" << retentionRate << '\n';

        if (argc == 4) {
            writeTrajectory(retained, argv[3], elapsedMs);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
