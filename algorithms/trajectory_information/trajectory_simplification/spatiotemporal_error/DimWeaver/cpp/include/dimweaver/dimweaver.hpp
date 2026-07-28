#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dimweaver {

struct TrajectoryPoint {
    std::int64_t t {};
    double x {};
    double y {};
};

struct TimeSeriesPoint {
    std::int64_t t {};
    double v {};
};

class Line {
public:
    Line() = default;
    Line(double t1, double v1, double t2, double v2);
    Line(double t_start, double a, double b);

    static Line middle_line(double t_start, const Line& upper, const Line& lower);

    double value_at(double t) const;

    double t_start() const;
    double a() const;
    double b() const;

private:
    double t_start_ {};
    double a_ {};
    double b_ {};
};

class Segment {
public:
    Segment(std::int64_t start, const Line& upper, const Line& lower, bool dim);

    std::int64_t start() const;
    const Line& upper() const;
    const Line& lower() const;
    const Line& middle() const;
    bool dim() const;

    double smallest_slope() const;
    double biggest_slope() const;
    void update_slopes(double smallest, double biggest);

private:
    std::int64_t start_ {};
    bool dim_ {};
    Line upper_ {};
    Line lower_ {};
    mutable Line middle_ {};
    mutable bool has_middle_ {false};
};

class BinaryCodec {
public:
    static std::vector<std::uint8_t> encode(const std::map<double, std::vector<Segment>>& grouped_segments,
                                            std::int64_t smallest_t);
    static std::vector<std::vector<Line>> decode(const std::vector<std::uint8_t>& binary);
};

std::vector<std::uint8_t> compress(const std::vector<TrajectoryPoint>& points, double epsilon, double c);
std::vector<std::vector<Line>> decompress(const std::vector<std::uint8_t>& binary);
std::vector<Segment> run_dimweaver(const std::vector<TrajectoryPoint>& points, double epsilon, double c);
std::vector<TrajectoryPoint> reconstruct_trajectory(const std::vector<std::vector<Line>>& lines,
                                                    const std::vector<TrajectoryPoint>& original);
std::vector<TrajectoryPoint> read_csv_trajectory(const std::string& path);
std::vector<TrajectoryPoint> read_tdrive_trajectory(const std::string& path);
double max_reconstruction_error(const std::vector<TrajectoryPoint>& original,
                                const std::vector<TrajectoryPoint>& reconstructed);

}  // namespace dimweaver
