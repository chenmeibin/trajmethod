#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ciseds {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-12;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct TrajectoryData {
    std::vector<double> times;
    std::vector<Point> points;
};

struct Metrics {
    std::size_t points = 0;
    std::size_t kept_points = 0;
    std::size_t compress_bits = 0;
    double compress_ratio = 0.0;
    double compressed_fraction = 0.0;
    double compression_time_us = 0.0;
    double max_error = 0.0;
    double mean_error = 0.0;
    double var_error = 0.0;
};

using Polygon = std::vector<Point>;

Point operator+(const Point& lhs, const Point& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

Point operator-(const Point& lhs, const Point& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

Point operator*(const Point& lhs, double scalar) {
    return {lhs.x * scalar, lhs.y * scalar};
}

Point operator/(const Point& lhs, double scalar) {
    return {lhs.x / scalar, lhs.y / scalar};
}

double dot(const Point& lhs, const Point& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

double cross(const Point& lhs, const Point& rhs) {
    return lhs.x * rhs.y - lhs.y * rhs.x;
}

double distance(const Point& lhs, const Point& rhs) {
    const double dx = lhs.x - rhs.x;
    const double dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool is_blank(const std::string& line) {
    for (unsigned char ch : line) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        parts.push_back(item);
    }
    return parts;
}

TrajectoryData load_simple_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open input file: " + path);
    }

    TrajectoryData data;
    std::string line;
    while (std::getline(in, line)) {
        if (is_blank(line)) {
            continue;
        }
        const auto parts = split_csv(line);
        if (parts.size() < 3) {
            throw std::runtime_error("Expected at least 3 columns (t,x,y) in: " + line);
        }

        const double t = std::stod(parts[0]);
        const double x = std::stod(parts[1]);
        const double y = std::stod(parts[2]);

        if (!data.times.empty() && std::abs(t - data.times.back()) < kEpsilon) {
            continue;
        }

        data.times.push_back(t);
        data.points.push_back({x, y});
    }

    if (data.points.size() < 2) {
        throw std::runtime_error("Trajectory must contain at least 2 valid points.");
    }
    return data;
}

float round_trip_float(double value) {
    return static_cast<float>(value);
}

double signed_area2(const Polygon& polygon) {
    double area2 = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point& a = polygon[i];
        const Point& b = polygon[(i + 1) % polygon.size()];
        area2 += cross(a, b);
    }
    return area2;
}

bool inside_half_plane(const Point& point, const Point& edge_start, const Point& edge_end, double orientation_sign) {
    const double side = cross(edge_end - edge_start, point - edge_start);
    return orientation_sign * side >= -1e-12;
}

Point line_intersection(const Point& p1, const Point& p2, const Point& q1, const Point& q2) {
    const Point r = p2 - p1;
    const Point s = q2 - q1;
    const double denom = cross(r, s);
    if (std::abs(denom) < kEpsilon) {
        return p2;
    }
    const double t = cross(q1 - p1, s) / denom;
    return p1 + r * t;
}

Polygon intersect_convex_polygons(const Polygon& subject, const Polygon& clip) {
    if (subject.empty() || clip.empty()) {
        return {};
    }

    Polygon output = subject;
    const double orientation_sign = signed_area2(clip) >= 0.0 ? 1.0 : -1.0;

    for (std::size_t i = 0; i < clip.size(); ++i) {
        const Point& a = clip[i];
        const Point& b = clip[(i + 1) % clip.size()];
        Polygon input = output;
        output.clear();
        if (input.empty()) {
            break;
        }

        Point prev = input.back();
        bool prev_inside = inside_half_plane(prev, a, b, orientation_sign);
        for (const Point& curr : input) {
            const bool curr_inside = inside_half_plane(curr, a, b, orientation_sign);
            if (curr_inside) {
                if (!prev_inside) {
                    output.push_back(line_intersection(prev, curr, a, b));
                }
                output.push_back(curr);
            } else if (prev_inside) {
                output.push_back(line_intersection(prev, curr, a, b));
            }
            prev = curr;
            prev_inside = curr_inside;
        }
    }

    if (output.size() < 3) {
        return {};
    }
    return output;
}

Polygon build_regular_polygon(const TrajectoryData& data, std::size_t index0, std::size_t index1, double radius, int polygon_num) {
    if (index1 <= index0 || index0 + 1 >= data.points.size()) {
        return {};
    }

    const double denominator = data.times[index1] - data.times[index0];
    if (std::abs(denominator) < kEpsilon) {
        return {};
    }

    const double c = (data.times[index0 + 1] - data.times[index0]) / denominator;
    const Point interpolated = data.points[index0] + (data.points[index1] - data.points[index0]) * c;

    Polygon polygon;
    polygon.reserve(static_cast<std::size_t>(polygon_num));
    for (int j = 0; j < polygon_num; ++j) {
        const double angle = (2.0 * static_cast<double>(j) - 1.0) * kPi / static_cast<double>(polygon_num);
        polygon.push_back({
            interpolated.x + radius * std::cos(angle) * c,
            interpolated.y + radius * std::sin(angle) * c
        });
    }
    return polygon;
}

std::vector<std::size_t> compress_indices(const TrajectoryData& data, double max_error, int polygon_num) {
    if (data.points.size() <= 2) {
        return {0, data.points.size() - 1};
    }

    std::vector<std::size_t> indices;
    indices.push_back(0);
    Polygon polygon;

    for (std::size_t i = 1; i < data.points.size(); ++i) {
        const Polygon temp = build_regular_polygon(data, indices.back(), i, max_error / 2.0, polygon_num);
        if (temp.empty()) {
            indices.push_back(i - 1);
            polygon = build_regular_polygon(data, indices.back(), i, max_error / 2.0, polygon_num);
            continue;
        }

        if (polygon.empty()) {
            polygon = temp;
        } else {
            Polygon next = intersect_convex_polygons(polygon, temp);
            if (next.empty()) {
                indices.push_back(i - 1);
                polygon = build_regular_polygon(data, indices.back(), i, max_error / 2.0, polygon_num);
            } else {
                polygon = std::move(next);
            }
        }
    }

    if (indices.back() != data.points.size() - 1) {
        indices.push_back(data.points.size() - 1);
    }
    return indices;
}

std::vector<Point> quantize_points(
    const TrajectoryData& data,
    const std::vector<std::size_t>& kept_indices,
    double max_error_dim) {
    std::vector<Point> quantized;
    quantized.reserve(kept_indices.size());
    for (std::size_t idx : kept_indices) {
        const Point& p = data.points[idx];
        const double qx = std::nearbyint(p.x / max_error_dim / 2.0) * max_error_dim * 2.0;
        const double qy = std::nearbyint(p.y / max_error_dim / 2.0) * max_error_dim * 2.0;
        quantized.push_back({qx, qy});
    }
    return quantized;
}

std::vector<Point> decompress_points(
    const TrajectoryData& data,
    const std::vector<std::size_t>& kept_indices,
    const std::vector<Point>& kept_points) {
    std::vector<Point> output;
    output.reserve(data.points.size());

    std::size_t segment = 0;
    for (std::size_t i = 0; i < data.points.size(); ++i) {
        const double t = data.times[i];
        while (segment + 1 < kept_indices.size() &&
               data.times[kept_indices[segment + 1]] < t - 1e-9) {
            ++segment;
        }

        const std::size_t left = kept_indices[segment];
        if (segment + 1 >= kept_indices.size()) {
            output.push_back(kept_points[segment]);
            continue;
        }

        const std::size_t right = kept_indices[segment + 1];
        const double dt = data.times[right] - data.times[left];
        if (std::abs(dt) < kEpsilon) {
            output.push_back(kept_points[segment]);
            continue;
        }

        const double ratio = (t - data.times[left]) / dt;
        output.push_back(kept_points[segment] + (kept_points[segment + 1] - kept_points[segment]) * ratio);
    }

    return output;
}

std::size_t varint_bits(std::uint64_t value, int data_length) {
    std::size_t bits = 0;
    do {
        value >>= data_length;
        bits += static_cast<std::size_t>(data_length + 1);
    } while (value > 0);
    return bits;
}

std::size_t zigzag_bits(std::int64_t value, int data_length) {
    std::uint64_t encoded = 0;
    if (value < 0) {
        encoded = static_cast<std::uint64_t>(-2 * value - 1);
    } else {
        encoded = static_cast<std::uint64_t>(2 * value);
    }
    return varint_bits(encoded, data_length);
}

std::size_t compute_compress_bits(
    const TrajectoryData& data,
    const std::vector<std::size_t>& kept_indices,
    const std::vector<Point>& quantized_points,
    double time_accuracy,
    double max_accuracy_error,
    int utf) {
    (void)max_accuracy_error;
    std::size_t bits = 64 + 32;
    if (kept_indices.empty()) {
        return bits;
    }

    double prev_time = 0.0;
    Point prev_point{0.0, 0.0};
    const double max_error_dim = max_accuracy_error / std::sqrt(2.0);

    for (std::size_t i = 0; i < kept_indices.size(); ++i) {
        const std::size_t index = kept_indices[i];
        const double curr_time = data.times[index];
        const std::int64_t delta_t = static_cast<std::int64_t>(std::llround(
            (i == 0 ? curr_time : (curr_time - prev_time)) / time_accuracy));
        bits += varint_bits(static_cast<std::uint64_t>(std::max<std::int64_t>(0, delta_t)), utf);

        const std::int64_t qx = static_cast<std::int64_t>(std::llround(quantized_points[i].x / max_error_dim / 2.0));
        const std::int64_t qy = static_cast<std::int64_t>(std::llround(quantized_points[i].y / max_error_dim / 2.0));
        const std::int64_t prev_qx = static_cast<std::int64_t>(std::llround(prev_point.x / max_error_dim / 2.0));
        const std::int64_t prev_qy = static_cast<std::int64_t>(std::llround(prev_point.y / max_error_dim / 2.0));

        bits += zigzag_bits(i == 0 ? qx : (qx - prev_qx), utf);
        bits += zigzag_bits(i == 0 ? qy : (qy - prev_qy), utf);

        prev_time = curr_time;
        prev_point = quantized_points[i];
    }
    return bits;
}

Metrics evaluate(
    const TrajectoryData& data,
    const std::vector<std::size_t>& kept_indices,
    double time_accuracy,
    double max_accuracy_error,
    int utf,
    double compression_time_us) {
    Metrics metrics;
    metrics.points = data.points.size();
    metrics.kept_points = kept_indices.size();
    const double max_error_dim = max_accuracy_error / std::sqrt(2.0);
    const auto quantized = quantize_points(data, kept_indices, max_error_dim);
    metrics.compress_bits = compute_compress_bits(data, kept_indices, quantized, time_accuracy, max_accuracy_error, utf);
    metrics.compress_ratio = metrics.compress_bits > 0
        ? static_cast<double>(metrics.points * 3 * 8 * 8) / static_cast<double>(metrics.compress_bits)
        : 0.0;
    metrics.compressed_fraction = metrics.compress_ratio > 0.0 ? 1.0 / metrics.compress_ratio : 0.0;
    metrics.compression_time_us = compression_time_us;

    const auto reconstructed = decompress_points(data, kept_indices, quantized);
    if (reconstructed.size() != data.points.size()) {
        throw std::runtime_error("Reconstruction size mismatch.");
    }

    double sum = 0.0;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < data.points.size(); ++i) {
        const double err = distance(data.points[i], reconstructed[i]);
        metrics.max_error = std::max(metrics.max_error, err);
        sum += err;
        sum_sq += err * err;
    }

    if (!data.points.empty()) {
        metrics.mean_error = sum / static_cast<double>(data.points.size());
        metrics.var_error = (sum_sq / static_cast<double>(data.points.size())) - (metrics.mean_error * metrics.mean_error);
        if (metrics.var_error < 0.0 && metrics.var_error > -1e-18) {
            metrics.var_error = 0.0;
        }
    }
    return metrics;
}

void write_kept_points(
    const std::string& output_path,
    const TrajectoryData& data,
    const std::vector<std::size_t>& kept_indices,
    const std::vector<Point>& kept_points) {
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Cannot write output file: " + output_path);
    }

    out << std::setprecision(17);
    out << "t,x,y\n";
    for (std::size_t i = 0; i < kept_indices.size(); ++i) {
        out << data.times[kept_indices[i]] << ',' << kept_points[i].x << ',' << kept_points[i].y << '\n';
    }
}

}  // namespace ciseds

int main(int argc, char** argv) {
    try {
        if (argc < 4 || argc > 7) {
            throw std::runtime_error("Usage: ciseds_reference.exe <input_csv> <max_error> <polygon_num> [output_csv] [accuracy] [utf]");
        }

        const std::string input_path = argv[1];
        const double max_error = std::stod(argv[2]);
        const int polygon_num = std::stoi(argv[3]);
        const std::string output_path = argc == 5 ? argv[4] : "";
        const double accuracy = argc >= 6 ? std::stod(argv[5]) : 0.2;
        const int utf = argc >= 7 ? std::stoi(argv[6]) : 3;
        const double time_accuracy = 1.0;

        if (max_error <= 0.0) {
            throw std::runtime_error("max_error must be positive.");
        }
        if (polygon_num < 3) {
            throw std::runtime_error("polygon_num must be at least 3.");
        }
        if (accuracy < 0.0 || accuracy >= 1.0) {
            throw std::runtime_error("accuracy must be in [0,1).");
        }

        const ciseds::TrajectoryData data = ciseds::load_simple_csv(input_path);
        const double rounded_max_error = static_cast<double>(ciseds::round_trip_float(max_error));
        const double max_accuracy_error = static_cast<double>(ciseds::round_trip_float(accuracy * rounded_max_error));
        const double working_error = rounded_max_error - max_accuracy_error;
        if (working_error <= 0.0) {
            throw std::runtime_error("working_error must be positive after subtracting accuracy reserve.");
        }
        const auto start = std::chrono::steady_clock::now();
        const auto kept_indices = ciseds::compress_indices(data, working_error, polygon_num);
        const auto end = std::chrono::steady_clock::now();
        const double compression_time_us =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        const auto quantized = ciseds::quantize_points(data, kept_indices, max_accuracy_error / std::sqrt(2.0));

        if (!output_path.empty()) {
            ciseds::write_kept_points(output_path, data, kept_indices, quantized);
        }

        const ciseds::Metrics metrics = ciseds::evaluate(
            data, kept_indices, time_accuracy, max_accuracy_error, utf, compression_time_us);

        std::cout << std::setprecision(17);
        std::cout << "points=" << metrics.points << '\n';
        std::cout << "kept_points=" << metrics.kept_points << '\n';
        std::cout << "compress_bits=" << metrics.compress_bits << '\n';
        std::cout << "compress_ratio=" << metrics.compress_ratio << '\n';
        std::cout << "compressed_fraction=" << metrics.compressed_fraction << '\n';
        std::cout << "compression_time_us=" << metrics.compression_time_us << '\n';
        std::cout << "max_error=" << metrics.max_error << '\n';
        std::cout << "mean_error=" << metrics.mean_error << '\n';
        std::cout << "var_error=" << metrics.var_error << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
