#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct PointRecord {
    double x = 0.0;
    double y = 0.0;
    double t = 0.0;
    bool has_t = false;
    std::string raw;
};

double point_to_segment_distance(double px, double py, double ax, double ay, double bx, double by) {
    const double abx = bx - ax;
    const double aby = by - ay;
    const double apx = px - ax;
    const double apy = py - ay;
    const double ab_sq = abx * abx + aby * aby;
    if (ab_sq < 1e-18) {
        const double dx = px - ax;
        const double dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    double u = (apx * abx + apy * aby) / ab_sq;
    u = std::clamp(u, 0.0, 1.0);
    const double cx = ax + u * abx;
    const double cy = ay + u * aby;
    const double dx = px - cx;
    const double dy = py - cy;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<PointRecord> read_input(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    std::vector<PointRecord> points;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream ss(line);
        PointRecord rec;
        if (!(ss >> rec.x >> rec.y)) {
            continue;
        }
        rec.raw = line;
        rec.has_t = static_cast<bool>(ss >> rec.t);
        points.push_back(rec);
    }
    return points;
}

std::vector<std::vector<double>> build_segment_error(const std::vector<PointRecord>& points) {
    const int n = static_cast<int>(points.size());
    std::vector<std::vector<double>> err(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double sum_sq = 0.0;
            for (int k = i + 1; k < j; ++k) {
                const double d = point_to_segment_distance(
                    points[k].x,
                    points[k].y,
                    points[i].x,
                    points[i].y,
                    points[j].x,
                    points[j].y);
                sum_sq += d * d;
            }
            err[i][j] = sum_sq;
        }
    }
    return err;
}

std::vector<int> bellman_simplify(const std::vector<PointRecord>& points, int kept_points_target, double* out_sse) {
    const int n = static_cast<int>(points.size());
    if (n <= 2 || kept_points_target >= n) {
        std::vector<int> keep(n);
        for (int i = 0; i < n; ++i) {
            keep[i] = i;
        }
        if (out_sse != nullptr) {
            *out_sse = 0.0;
        }
        return keep;
    }
    kept_points_target = std::max(2, kept_points_target);

    const auto err = build_segment_error(points);
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(kept_points_target + 1, std::vector<double>(n, inf));
    std::vector<std::vector<int>> prev(kept_points_target + 1, std::vector<int>(n, -1));

    dp[1][0] = 0.0;

    for (int kept = 2; kept <= kept_points_target; ++kept) {
        for (int j = kept - 1; j < n; ++j) {
            for (int i = kept - 2; i < j; ++i) {
                if (!std::isfinite(dp[kept - 1][i])) {
                    continue;
                }
                const double candidate = dp[kept - 1][i] + err[i][j];
                if (candidate < dp[kept][j]) {
                    dp[kept][j] = candidate;
                    prev[kept][j] = i;
                }
            }
        }
    }

    int used_kept = kept_points_target;
    while (used_kept > 2 && !std::isfinite(dp[used_kept][n - 1])) {
        --used_kept;
    }
    if (out_sse != nullptr) {
        *out_sse = dp[used_kept][n - 1];
    }

    std::vector<int> keep;
    keep.push_back(n - 1);
    int j = n - 1;
    for (int kept = used_kept; kept >= 2; --kept) {
        const int i = prev[kept][j];
        if (i < 0) {
            break;
        }
        keep.push_back(i);
        j = i;
    }
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    return keep;
}

void write_output(
    const std::string& path,
    const std::vector<PointRecord>& points,
    const std::vector<int>& keep,
    double runtime_seconds,
    double rmse) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + path);
    }
    for (int idx : keep) {
        out << points[idx].raw << '\n';
    }
    out << "# runtime_seconds=" << std::fixed << std::setprecision(6) << runtime_seconds << '\n';
    out << "# rmse=" << std::fixed << std::setprecision(6) << rmse << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: bellman <input_file> <num_kept_points> <output_file>\n";
        return 1;
    }

    try {
        const std::string input_file = argv[1];
        const int num_kept_points = std::stoi(argv[2]);
        const std::string output_file = argv[3];

        const auto points = read_input(input_file);
        if (points.size() < 2) {
            throw std::runtime_error("input trajectory must contain at least two points");
        }

        double sse = 0.0;
        const auto start = std::chrono::steady_clock::now();
        const auto keep = bellman_simplify(points, num_kept_points, &sse);
        const auto end = std::chrono::steady_clock::now();
        const double runtime_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        const double rmse = std::sqrt(sse / static_cast<double>(points.size()));

        write_output(output_file, points, keep, runtime_seconds, rmse);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
