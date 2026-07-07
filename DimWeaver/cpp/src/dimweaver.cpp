#include "dimweaver/dimweaver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace dimweaver {
namespace {

class ConvexHull {
public:
    void add_to_hull(const TimeSeriesPoint& point, double epsilon) {
        upper_hull_.push_back(TimeSeriesPoint{point.t, point.v + epsilon});
        lower_hull_.push_back(TimeSeriesPoint{point.t, point.v - epsilon});
        update_hull(upper_hull_, true);
        update_hull(lower_hull_, false);
    }

    std::size_t size(bool upper) const {
        return upper ? upper_hull_.size() : lower_hull_.size();
    }

    const TimeSeriesPoint& get(std::size_t index, bool upper) const {
        return upper ? upper_hull_.at(index) : lower_hull_.at(index);
    }

    void clear() {
        upper_hull_.clear();
        lower_hull_.clear();
    }

private:
    static double cross_product(const TimeSeriesPoint& o, const TimeSeriesPoint& a, const TimeSeriesPoint& b) {
        return static_cast<double>(a.t - o.t) * (b.v - o.v) - (a.v - o.v) * static_cast<double>(b.t - o.t);
    }

    static void update_hull(std::vector<TimeSeriesPoint>& hull, bool is_upper) {
        while (hull.size() > 2) {
            const auto size = hull.size();
            const TimeSeriesPoint& a = hull[size - 3];
            const TimeSeriesPoint& b = hull[size - 2];
            const TimeSeriesPoint& c = hull[size - 1];
            const double cross = cross_product(a, b, c);
            if ((is_upper && cross < 0.0) || (!is_upper && cross > 0.0)) {
                hull.erase(hull.end() - 2);
            } else {
                break;
            }
        }
    }

    std::vector<TimeSeriesPoint> upper_hull_;
    std::vector<TimeSeriesPoint> lower_hull_;
};

class DisjointPLA {
public:
    explicit DisjointPLA(bool y) : y_(y) {}

    bool add(const TimeSeriesPoint& new_point, double epsilon) {
        if (clear_next_) {
            clear();
        }
        clear_next_ = false;
        segment_points_.push_back(new_point);

        if (segment_points_.size() == 1) {
            hull_.add_to_hull(new_point, epsilon);
            return true;
        }

        if (segment_points_.size() == 2) {
            lower_ = Line(hull_.get(0, true).t, hull_.get(0, true).v, segment_points_[1].t, segment_points_[1].v - epsilon);
            upper_ = Line(hull_.get(0, false).t, hull_.get(0, false).v, segment_points_[1].t, segment_points_[1].v + epsilon);
            hull_.add_to_hull(new_point, epsilon);
            return true;
        }

        if (new_point.v - epsilon > upper_.value_at(static_cast<double>(new_point.t)) ||
            new_point.v + epsilon < lower_.value_at(static_cast<double>(new_point.t))) {
            segments_.emplace_back(segment_points_.front().t, upper_, lower_, y_);
            segment_points_.pop_back();
            clear_next_ = true;
            return false;
        }

        hull_.add_to_hull(new_point, epsilon);
        if (lower_.value_at(static_cast<double>(new_point.t)) + epsilon < new_point.v) {
            for (std::size_t i = 0; i + 1 < hull_.size(true); ++i) {
                Line temp(hull_.get(i, true).t, hull_.get(i, true).v, segment_points_.back().t,
                          segment_points_.back().v - epsilon);
                if (temp.a() > lower_.a()) {
                    lower_ = temp;
                }
            }
        }
        if (upper_.value_at(static_cast<double>(new_point.t)) - epsilon > new_point.v) {
            for (std::size_t i = 0; i + 1 < hull_.size(false); ++i) {
                Line temp(hull_.get(i, false).t, hull_.get(i, false).v, segment_points_.back().t,
                          segment_points_.back().v + epsilon);
                if (temp.a() < upper_.a()) {
                    upper_ = temp;
                }
            }
        }
        return true;
    }

    void close_segment() {
        if (segment_points_.size() == 1) {
            segments_.emplace_back(segment_points_.front().t,
                                   Line(static_cast<double>(segment_points_.front().t), 0.0, hull_.get(0, true).v),
                                   Line(static_cast<double>(segment_points_.front().t), 0.0, hull_.get(0, false).v),
                                   y_);
        } else if (segment_points_.size() > 1) {
            segments_.emplace_back(segment_points_.front().t, upper_, lower_, y_);
        }
        segment_points_.clear();
        hull_.clear();
    }

    std::vector<Segment>& segments() {
        return segments_;
    }

    const std::vector<Segment>& segments() const {
        return segments_;
    }

    int segment_size() const {
        return static_cast<int>(segment_points_.size());
    }

private:
    void clear() {
        segment_points_.clear();
        lower_ = Line();
        upper_ = Line();
        hull_.clear();
    }

    bool y_ {};
    ConvexHull hull_ {};
    std::vector<Segment> segments_ {};
    std::vector<TimeSeriesPoint> segment_points_ {};
    Line lower_ {};
    Line upper_ {};
    bool clear_next_ {false};
};

double calculate_residual(const Segment& segment, const TimeSeriesPoint& point, double epsilon) {
    const double upper_error = std::abs(segment.upper().value_at(static_cast<double>(point.t)) - point.v);
    const double lower_error = std::abs(segment.lower().value_at(static_cast<double>(point.t)) - point.v);
    const double max_error = std::max(upper_error, lower_error);
    const double residual = std::sqrt(std::max(0.0, epsilon * epsilon - max_error * max_error));
    return std::max(residual, epsilon / std::sqrt(2.0));
}

std::map<double, std::vector<Segment>> group_segments(std::vector<Segment> segments) {
    std::map<double, std::vector<Segment>> grouped_segments;
    double a_lower_group = -std::numeric_limits<double>::max();
    double a_upper_group = std::numeric_limits<double>::max();
    std::vector<Segment> group;

    std::sort(segments.begin(), segments.end(),
              [](const Segment& lhs, const Segment& rhs) { return lhs.smallest_slope() < rhs.smallest_slope(); });

    for (auto& segment : segments) {
        if (segment.smallest_slope() == 0.0 && segment.biggest_slope() == 0.0) {
            grouped_segments[0.0].push_back(segment);
            continue;
        }

        if (group.empty() ||
            (segment.smallest_slope() <= a_upper_group && segment.biggest_slope() >= a_lower_group)) {
            a_lower_group = std::max(a_lower_group, segment.smallest_slope());
            a_upper_group = std::min(a_upper_group, segment.biggest_slope());
            group.push_back(segment);
            continue;
        }

        for (auto& grouped_segment : group) {
            grouped_segment.update_slopes(a_lower_group, a_upper_group);
        }
        const double key = group.front().middle().a();
        auto& existing = grouped_segments[key];
        existing.insert(existing.end(), group.begin(), group.end());

        group.clear();
        group.push_back(segment);
        a_lower_group = segment.smallest_slope();
        a_upper_group = segment.biggest_slope();
    }

    if (!group.empty()) {
        for (auto& grouped_segment : group) {
            grouped_segment.update_slopes(a_lower_group, a_upper_group);
        }
        const double key = group.front().middle().a();
        auto& existing = grouped_segments[key];
        existing.insert(existing.end(), group.begin(), group.end());
    }

    return grouped_segments;
}

void write_varint(std::uint64_t value, std::vector<std::uint8_t>& out) {
    while ((value & ~0x7FULL) != 0ULL) {
        out.push_back(static_cast<std::uint8_t>(value & 0x7F));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value | 0x80U));
}

std::uint64_t read_varint(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    std::uint64_t result = 0;
    int shift = 0;
    while (pos < in.size()) {
        const std::uint8_t byte = in[pos++];
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80U) != 0U) {
            return result;
        }
        shift += 7;
    }
    throw std::runtime_error("Invalid varint encoding");
}

void write_bool_vector(const std::vector<bool>& bits, std::vector<std::uint8_t>& out) {
    const std::size_t byte_count = (bits.size() + 7U) / 8U;
    write_varint(byte_count, out);
    const std::size_t offset = out.size();
    out.resize(out.size() + byte_count, 0);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            out[offset + (i / 8U)] |= static_cast<std::uint8_t>(1U << (i % 8U));
        }
    }
}

std::vector<bool> read_bool_vector(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    const std::size_t byte_count = static_cast<std::size_t>(read_varint(in, pos));
    if (pos + byte_count > in.size()) {
        throw std::runtime_error("Invalid dimension metadata");
    }
    std::vector<bool> bits(byte_count * 8U, false);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] = ((in[pos + (i / 8U)] >> (i % 8U)) & 1U) != 0U;
    }
    pos += byte_count;
    return bits;
}

void write_float(float value, std::vector<std::uint8_t>& out) {
    const auto* ptr = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(float));
}

float read_float(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    if (pos + sizeof(float) > in.size()) {
        throw std::runtime_error("Unexpected end of input while reading float");
    }
    float value {};
    auto* ptr = reinterpret_cast<std::uint8_t*>(&value);
    std::copy(in.begin() + static_cast<std::ptrdiff_t>(pos),
              in.begin() + static_cast<std::ptrdiff_t>(pos + sizeof(float)), ptr);
    pos += sizeof(float);
    return value;
}

void write_double(double value, std::vector<std::uint8_t>& out) {
    const auto* ptr = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(double));
}

double read_double(const std::vector<std::uint8_t>& in, std::size_t& pos) {
    if (pos + sizeof(double) > in.size()) {
        throw std::runtime_error("Unexpected end of input while reading double");
    }
    double value {};
    auto* ptr = reinterpret_cast<std::uint8_t*>(&value);
    std::copy(in.begin() + static_cast<std::ptrdiff_t>(pos),
              in.begin() + static_cast<std::ptrdiff_t>(pos + sizeof(double)), ptr);
    pos += sizeof(double);
    return value;
}

struct MergeEntry {
    double t {};
    std::size_t idx {};
    std::size_t end {};
};

std::vector<Line> merge_k_way(const std::vector<Line>& lines,
                              const std::vector<std::pair<std::size_t, std::size_t>>& subgroups) {
    std::vector<Line> result;
    result.reserve(lines.size());

    auto cmp = [](const MergeEntry& lhs, const MergeEntry& rhs) { return lhs.t > rhs.t; };
    std::priority_queue<MergeEntry, std::vector<MergeEntry>, decltype(cmp)> pq(cmp);

    for (const auto& subgroup : subgroups) {
        if (subgroup.first < subgroup.second) {
            pq.push(MergeEntry{lines[subgroup.first].t_start(), subgroup.first, subgroup.second});
        }
    }

    while (!pq.empty()) {
        MergeEntry entry = pq.top();
        pq.pop();
        result.push_back(lines[entry.idx]);
        ++entry.idx;
        if (entry.idx < entry.end) {
            entry.t = lines[entry.idx].t_start();
            pq.push(entry);
        }
    }

    return result;
}

}  // namespace

Line::Line(double t1, double v1, double t2, double v2) : t_start_(t1), a_((v2 - v1) / (t2 - t1)), b_(v1) {}

Line::Line(double t_start, double a, double b) : t_start_(t_start), a_(a), b_(b) {}

Line Line::middle_line(double t_start, const Line& upper, const Line& lower) {
    if (upper.a() == lower.a()) {
        return Line(t_start, upper.a(), (lower.b() + upper.b()) / 2.0);
    }
    const double intersection_t =
        (upper.a() * upper.t_start() - upper.b() - lower.a() * lower.t_start() + lower.b()) / (upper.a() - lower.a());
    const double intersection_v = lower.a() * (intersection_t - lower.t_start()) + lower.b();
    const double a_middle = std::tan((std::atan(lower.a()) + std::atan(upper.a())) / 2.0);
    return Line(t_start, a_middle, intersection_v - a_middle * (intersection_t - t_start));
}

double Line::value_at(double t) const {
    return a_ * (t - t_start_) + b_;
}

double Line::t_start() const { return t_start_; }
double Line::a() const { return a_; }
double Line::b() const { return b_; }

Segment::Segment(std::int64_t start, const Line& upper, const Line& lower, bool dim) : start_(start), dim_(dim) {
    upper_ = (upper.t_start() != static_cast<double>(start)) ? Line(static_cast<double>(start), upper.a(), upper.value_at(static_cast<double>(start))) : upper;
    lower_ = (lower.t_start() != static_cast<double>(start)) ? Line(static_cast<double>(start), lower.a(), lower.value_at(static_cast<double>(start))) : lower;
}

std::int64_t Segment::start() const { return start_; }
const Line& Segment::upper() const { return upper_; }
const Line& Segment::lower() const { return lower_; }
bool Segment::dim() const { return dim_; }

const Line& Segment::middle() const {
    if (!has_middle_) {
        middle_ = Line::middle_line(static_cast<double>(start_), upper_, lower_);
        has_middle_ = true;
    }
    return middle_;
}

double Segment::smallest_slope() const {
    return std::min(lower_.a(), upper_.a());
}

double Segment::biggest_slope() const {
    return std::max(lower_.a(), upper_.a());
}

void Segment::update_slopes(double smallest, double biggest) {
    double a_low;
    double a_up;
    if (lower_.a() < upper_.a()) {
        a_low = smallest;
        a_up = biggest;
    } else if (lower_.a() > upper_.a()) {
        a_low = biggest;
        a_up = smallest;
    } else {
        return;
    }
    const double intersection_t =
        (upper_.a() * upper_.t_start() - upper_.b() - lower_.a() * lower_.t_start() + lower_.b()) / (upper_.a() - lower_.a());
    const double intersection_v = lower_.a() * (intersection_t - lower_.t_start()) + lower_.b();
    lower_ = Line(lower_.t_start(), a_low, intersection_v - a_low * (intersection_t - lower_.t_start()));
    upper_ = Line(upper_.t_start(), a_up, intersection_v - a_up * (intersection_t - upper_.t_start()));
    has_middle_ = false;
}

std::vector<std::uint8_t> BinaryCodec::encode(const std::map<double, std::vector<Segment>>& grouped_segments,
                                              std::int64_t smallest_t) {
    std::size_t total_segments = 0;
    for (const auto& entry : grouped_segments) {
        total_segments += entry.second.size();
    }

    std::vector<bool> dimensions;
    dimensions.reserve(total_segments);
    std::vector<std::uint64_t> counters;
    counters.reserve(grouped_segments.size());
    std::vector<std::uint8_t> out;

    write_varint(static_cast<std::uint64_t>(smallest_t), out);
    write_varint(static_cast<std::uint64_t>(grouped_segments.size()), out);

    std::vector<std::uint8_t> group_payload;
    for (const auto& entry : grouped_segments) {
        auto segments = entry.second;
        std::sort(segments.begin(), segments.end(),
                  [](const Segment& lhs, const Segment& rhs) { return lhs.start() < rhs.start(); });
        counters.push_back(static_cast<std::uint64_t>(segments.size()));
        write_float(static_cast<float>(segments.front().middle().a()), group_payload);
        std::int64_t previous_t = 0;
        for (const auto& segment : segments) {
            write_double(segment.middle().b(), group_payload);
            write_varint(static_cast<std::uint64_t>(segment.start() - previous_t - smallest_t), group_payload);
            previous_t = segment.start() - smallest_t;
            dimensions.push_back(segment.dim());
        }
    }

    write_bool_vector(dimensions, out);
    for (std::uint64_t counter : counters) {
        write_varint(counter, out);
    }
    out.insert(out.end(), group_payload.begin(), group_payload.end());
    return out;
}

std::vector<std::vector<Line>> BinaryCodec::decode(const std::vector<std::uint8_t>& binary) {
    std::size_t pos = 0;
    const std::int64_t smallest_t = static_cast<std::int64_t>(read_varint(binary, pos));
    const std::size_t group_count = static_cast<std::size_t>(read_varint(binary, pos));
    const std::vector<bool> dimensions = read_bool_vector(binary, pos);

    std::vector<std::uint64_t> counters(group_count, 0);
    std::size_t total_timestamps = 0;
    for (std::size_t i = 0; i < group_count; ++i) {
        counters[i] = read_varint(binary, pos);
        total_timestamps += static_cast<std::size_t>(counters[i]);
    }
    if (dimensions.size() < total_timestamps) {
        throw std::runtime_error("Dimension metadata shorter than segment count");
    }

    std::vector<Line> lines_x;
    std::vector<Line> lines_y;
    lines_x.reserve(total_timestamps);
    lines_y.reserve(total_timestamps);
    std::vector<std::pair<std::size_t, std::size_t>> subgroups_x;
    std::vector<std::pair<std::size_t, std::size_t>> subgroups_y;
    std::size_t dim_idx = 0;

    for (std::size_t group_idx = 0; group_idx < group_count; ++group_idx) {
        const double a = static_cast<double>(read_float(binary, pos));
        std::int64_t previous_t = 0;
        const std::size_t start_x = lines_x.size();
        const std::size_t start_y = lines_y.size();

        for (std::size_t i = 0; i < counters[group_idx]; ++i) {
            const double b = read_double(binary, pos);
            const std::int64_t t =
                static_cast<std::int64_t>(read_varint(binary, pos)) + previous_t + smallest_t;
            if (!dimensions[dim_idx]) {
                lines_x.emplace_back(static_cast<double>(t), a, b);
            } else {
                lines_y.emplace_back(static_cast<double>(t), a, b);
            }
            previous_t = t - smallest_t;
            ++dim_idx;
        }

        subgroups_x.emplace_back(start_x, lines_x.size());
        subgroups_y.emplace_back(start_y, lines_y.size());
    }

    return {merge_k_way(lines_x, subgroups_x), merge_k_way(lines_y, subgroups_y)};
}

std::vector<std::uint8_t> compress(const std::vector<TrajectoryPoint>& points, double epsilon, double c) {
    if (points.empty()) {
        return {};
    }
    return BinaryCodec::encode(group_segments(run_dimweaver(points, epsilon, c)), points.front().t);
}

std::vector<std::vector<Line>> decompress(const std::vector<std::uint8_t>& binary) {
    if (binary.empty()) {
        return {{}, {}};
    }
    return BinaryCodec::decode(binary);
}

std::vector<Segment> run_dimweaver(const std::vector<TrajectoryPoint>& points, double epsilon, double c) {
    DisjointPLA disjoint_pla_x(false);
    DisjointPLA disjoint_pla_y(true);
    std::vector<TimeSeriesPoint> buffer_x;
    std::vector<TimeSeriesPoint> buffer_y;
    double exponential_decay_x = 0.0;
    double exponential_decay_y = 0.0;
    bool residual_y = false;

    for (const auto& point : points) {
        TimeSeriesPoint point_x{point.t, point.x};
        TimeSeriesPoint point_y{point.t, point.y};
        buffer_x.push_back(point_x);
        buffer_y.push_back(point_y);

        bool added = residual_y ? disjoint_pla_x.add(point_x, epsilon / std::sqrt(2.0))
                                : disjoint_pla_y.add(point_y, epsilon / std::sqrt(2.0));
        if (added) {
            continue;
        }

        if (residual_y) {
            exponential_decay_x = exponential_decay_x * (1.0 - c) + disjoint_pla_x.segment_size() * c;
        } else {
            exponential_decay_y = exponential_decay_y * (1.0 - c) + disjoint_pla_y.segment_size() * c;
        }

        const Segment& last_segment =
            residual_y ? disjoint_pla_x.segments().back() : disjoint_pla_y.segments().back();
        const std::vector<TimeSeriesPoint>& buffer_uncompressed = residual_y ? buffer_y : buffer_x;
        const std::vector<TimeSeriesPoint>& buffer_compressed = residual_y ? buffer_x : buffer_y;

        for (std::size_t i = 0; i + 1 < buffer_uncompressed.size(); ++i) {
            const double residual = calculate_residual(last_segment, buffer_compressed[i], epsilon);
            added = residual_y ? disjoint_pla_y.add(buffer_uncompressed[i], residual)
                               : disjoint_pla_x.add(buffer_uncompressed[i], residual);
            if (!added) {
                if (residual_y) {
                    exponential_decay_y = exponential_decay_y * (1.0 - c) + disjoint_pla_y.segment_size() * c;
                    disjoint_pla_y.add(buffer_uncompressed[i], residual);
                } else {
                    exponential_decay_x = exponential_decay_x * (1.0 - c) + disjoint_pla_x.segment_size() * c;
                    disjoint_pla_x.add(buffer_uncompressed[i], residual);
                }
            }
        }

        residual_y = exponential_decay_x * c > exponential_decay_y * c;
        added = residual_y ? disjoint_pla_x.add(point_x, epsilon / std::sqrt(2.0))
                           : disjoint_pla_y.add(point_y, epsilon / std::sqrt(2.0));
        if (!added) {
            if (residual_y) {
                exponential_decay_x = exponential_decay_x * (1.0 - c) + disjoint_pla_x.segment_size() * c;
            } else {
                exponential_decay_y = exponential_decay_y * (1.0 - c) + disjoint_pla_y.segment_size() * c;
            }
            residual_y = exponential_decay_x * c > exponential_decay_y * c;
            if (residual_y) {
                disjoint_pla_x.add(point_x, epsilon / std::sqrt(2.0));
            } else {
                disjoint_pla_y.add(point_y, epsilon / std::sqrt(2.0));
            }
        }

        buffer_x.clear();
        buffer_x.push_back(point_x);
        buffer_y.clear();
        buffer_y.push_back(point_y);
    }

    if (residual_y) {
        disjoint_pla_x.close_segment();
        const Segment& last_segment = disjoint_pla_x.segments().back();
        for (std::size_t i = 0; i < buffer_y.size(); ++i) {
            const double residual = calculate_residual(last_segment, buffer_x[i], epsilon);
            if (!disjoint_pla_y.add(buffer_y[i], residual)) {
                disjoint_pla_y.add(buffer_y[i], residual);
            }
        }
        disjoint_pla_y.close_segment();
    } else {
        disjoint_pla_y.close_segment();
        const Segment& last_segment = disjoint_pla_y.segments().back();
        for (std::size_t i = 0; i < buffer_x.size(); ++i) {
            const double residual = calculate_residual(last_segment, buffer_y[i], epsilon);
            if (!disjoint_pla_x.add(buffer_x[i], residual)) {
                disjoint_pla_x.add(buffer_x[i], residual);
            }
        }
        disjoint_pla_x.close_segment();
    }

    auto segments = disjoint_pla_x.segments();
    const auto& y_segments = disjoint_pla_y.segments();
    segments.insert(segments.end(), y_segments.begin(), y_segments.end());
    return segments;
}

std::vector<TrajectoryPoint> reconstruct_trajectory(const std::vector<std::vector<Line>>& lines,
                                                    const std::vector<TrajectoryPoint>& original) {
    if (lines.size() < 2) {
        return {};
    }
    std::vector<TrajectoryPoint> reconstructed;
    reconstructed.reserve(original.size());

    std::size_t x_idx = 0;
    std::size_t y_idx = 0;
    const std::size_t num_x = lines[0].size();
    const std::size_t num_y = lines[1].size();
    if (num_x == 0 || num_y == 0) {
        return {};
    }

    std::int64_t next_timestamp_x = static_cast<std::int64_t>(lines[0][std::min<std::size_t>(x_idx + 1, num_x - 1)].t_start());
    std::int64_t next_timestamp_y = static_cast<std::int64_t>(lines[1][std::min<std::size_t>(y_idx + 1, num_y - 1)].t_start());
    Line middle_x = lines[0][x_idx];
    Line middle_y = lines[1][y_idx];

    for (const auto& point : original) {
        const std::int64_t t = point.t;
        if (x_idx + 1 < num_x && next_timestamp_x <= t) {
            ++x_idx;
            next_timestamp_x = static_cast<std::int64_t>(lines[0][std::min<std::size_t>(x_idx + 1, num_x - 1)].t_start());
            middle_x = lines[0][x_idx];
        }
        if (y_idx + 1 < num_y && next_timestamp_y <= t) {
            ++y_idx;
            next_timestamp_y = static_cast<std::int64_t>(lines[1][std::min<std::size_t>(y_idx + 1, num_y - 1)].t_start());
            middle_y = lines[1][y_idx];
        }
        reconstructed.push_back(TrajectoryPoint{t, middle_x.value_at(static_cast<double>(t)), middle_y.value_at(static_cast<double>(t))});
    }

    return reconstructed;
}

std::vector<TrajectoryPoint> read_csv_trajectory(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::vector<TrajectoryPoint> trajectory;
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> parts;
        while (std::getline(ss, field, ',')) {
            parts.push_back(field);
        }
        if (parts.size() < 3) {
            continue;
        }
        trajectory.push_back(TrajectoryPoint{std::stoll(parts[0]), std::stod(parts[1]), std::stod(parts[2])});
    }
    return trajectory;
}

std::vector<TrajectoryPoint> read_tdrive_trajectory(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::vector<TrajectoryPoint> trajectory;
    std::string line;
    bool initialized = false;
    std::time_t start_time = 0;
    std::int64_t previous_t = std::numeric_limits<std::int64_t>::min();

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> parts;
        while (std::getline(ss, field, ',')) {
            parts.push_back(field);
        }
        if (parts.size() < 4) {
            continue;
        }

        std::tm tm = {};
        std::istringstream ts(parts[1]);
        ts >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ts.fail()) {
            throw std::runtime_error("Failed to parse T-drive timestamp: " + parts[1]);
        }
        tm.tm_isdst = -1;
        const std::time_t current_time = std::mktime(&tm);
        if (!initialized) {
            initialized = true;
            start_time = current_time;
        }
        const std::int64_t t = static_cast<std::int64_t>(std::difftime(current_time, start_time));
        if (!trajectory.empty() && t == previous_t) {
            continue;
        }
        trajectory.push_back(TrajectoryPoint{t, std::stod(parts[2]), std::stod(parts[3])});
        previous_t = t;
    }

    return trajectory;
}

double max_reconstruction_error(const std::vector<TrajectoryPoint>& original,
                                const std::vector<TrajectoryPoint>& reconstructed) {
    double max_error = 0.0;
    const std::size_t count = std::min(original.size(), reconstructed.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double dx = reconstructed[i].x - original[i].x;
        const double dy = reconstructed[i].y - original[i].y;
        max_error = std::max(max_error, std::sqrt(dx * dx + dy * dy));
    }
    return max_error;
}

}  // namespace dimweaver
