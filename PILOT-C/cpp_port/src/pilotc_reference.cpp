#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pilotc {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusMeters = 6371000.0;

struct TrajectoryData {
    std::vector<double> t_list;
    std::vector<std::vector<double>> traj;
    bool projected_from_degrees = false;
    double reference_latitude_degrees = std::numeric_limits<double>::quiet_NaN();
    double meters_per_degree_latitude = 0.0;
    double meters_per_degree_longitude = 0.0;
};

struct Config {
    std::string data_source = "test_scalability";
    double scale_factor = 0.3;
    int utf_default = 3;
    double error_default = 50.0;
    double a = 0.5;
    double b = 25.0;
    double c = 1.1;
    double time_unit = 3.0;
    double time_accuracy = 0.001;
    double accuracy_default = 0.5;
};

struct Metrics {
    double max_error = 0.0;
    double mean_error = 0.0;
    double compress_ratio = 0.0;
    std::size_t length = 0;
    std::size_t error_cnt = 0;
    double var_error = 0.0;
    double compress_time_ms = 0.0;
    double decompress_time_ms = 0.0;
    double pipeline_time_ms = 0.0;
};

double round_even(double value) {
    return std::nearbyint(value);
}

Config make_config(const std::string& data_source) {
    Config cfg;
    cfg.data_source = data_source;
    if (data_source == "geolife") {
        cfg.time_accuracy = 1.0;
    } else if (data_source == "geolife_3d") {
        cfg.time_accuracy = 1.0;
        cfg.scale_factor = 0.35;
        cfg.c = 0.8;
    } else if (data_source == "mopsi") {
        cfg.time_accuracy = 0.001;
        cfg.a = 1.0;
        cfg.c = 0.6;
        cfg.time_unit = 2.0;
    } else if (data_source == "nuplan") {
        cfg.time_accuracy = 0.01;
        cfg.error_default = 5.0;
        cfg.a = 20.0;
        cfg.b = 100.0;
        cfg.c = 0.04;
        cfg.time_unit = 0.1;
    } else if (data_source == "test_scalability") {
        cfg.time_accuracy = 0.001;
    } else {
        throw std::runtime_error("Unsupported data source: " + data_source);
    }
    return cfg;
}

std::vector<std::string> split(const std::string& line, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

bool is_blank(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
}

TrajectoryData load_simple_traj(const std::string& path) {
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
        auto parts = split(line, ',');
        if (parts.size() < 3) {
            throw std::runtime_error("Expected at least 3 columns in: " + line);
        }
        data.t_list.push_back(std::stod(parts[0]));
        std::vector<double> point;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            point.push_back(std::stod(parts[i]));
        }
        data.traj.push_back(std::move(point));
    }
    return data;
}

std::time_t parse_tdrive_time(const std::string& value) {
    std::tm tm{};
    std::istringstream ss(value);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        throw std::runtime_error("Cannot parse T-drive timestamp: " + value);
    }
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

TrajectoryData load_tdrive_traj(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open T-drive input file: " + path);
    }

    TrajectoryData data;
    data.projected_from_degrees = true;

    std::string line;
    bool initialized = false;
    std::time_t start_time = 0;
    double ref_lat_rad = 0.0;
    double ref_lon_rad = 0.0;
    double previous_t = std::numeric_limits<double>::quiet_NaN();

    while (std::getline(in, line)) {
        if (is_blank(line)) {
            continue;
        }
        auto parts = split(line, ',');
        if (parts.size() < 4) {
            throw std::runtime_error("Expected T-drive columns taxi_id,time,lon,lat in: " + line);
        }

        const auto current_time = parse_tdrive_time(parts[1]);
        const double lon = std::stod(parts[2]);
        const double lat = std::stod(parts[3]);
        const double lat_rad = lat * kPi / 180.0;
        const double lon_rad = lon * kPi / 180.0;

        if (!initialized) {
            initialized = true;
            start_time = current_time;
            ref_lat_rad = lat_rad;
            ref_lon_rad = lon_rad;
            data.reference_latitude_degrees = lat;
            data.meters_per_degree_latitude = kEarthRadiusMeters * kPi / 180.0;
            data.meters_per_degree_longitude = data.meters_per_degree_latitude * std::cos(ref_lat_rad);
        }

        const double t = std::difftime(current_time, start_time);
        if (!std::isnan(previous_t) && std::abs(t - previous_t) < 1e-9) {
            continue;
        }

        const double x = (lon_rad - ref_lon_rad) * std::cos(ref_lat_rad) * kEarthRadiusMeters;
        const double y = (lat_rad - ref_lat_rad) * kEarthRadiusMeters;
        data.t_list.push_back(t);
        data.traj.push_back({x, y});
        previous_t = t;
    }

    return data;
}

TrajectoryData load_nuplan_traj(const std::string& dir_path) {
    std::ifstream in(dir_path + "/ego.csv");
    if (!in) {
        throw std::runtime_error("Cannot open nuplan ego.csv under: " + dir_path);
    }

    std::string header_line;
    if (!std::getline(in, header_line)) {
        throw std::runtime_error("nuplan ego.csv is empty");
    }
    auto header = split(header_line, ',');
    std::unordered_map<std::string, std::size_t> idx;
    for (std::size_t i = 0; i < header.size(); ++i) {
        idx[header[i]] = i;
    }

    auto require_index = [&](const std::string& name) -> std::size_t {
        auto it = idx.find(name);
        if (it == idx.end()) {
            throw std::runtime_error("Missing column in ego.csv: " + name);
        }
        return it->second;
    };

    const auto x_idx = require_index("X_utm(m)");
    const auto y_idx = require_index("Y_utm(m)");
    const auto t_idx = require_index("SimTime(s)");

    TrajectoryData data;
    std::string line;
    while (std::getline(in, line)) {
        if (is_blank(line)) {
            continue;
        }
        auto parts = split(line, ',');
        if (parts.size() <= std::max({x_idx, y_idx, t_idx})) {
            continue;
        }
        data.t_list.push_back(std::stod(parts[t_idx]));
        data.traj.push_back({std::stod(parts[x_idx]), std::stod(parts[y_idx])});
    }
    return data;
}

TrajectoryData load_trajectory(const std::string& input, const std::string& data_source, const std::string& input_format) {
    if (input_format == "tdrive") {
        return load_tdrive_traj(input);
    }
    if (input_format != "simple") {
        throw std::runtime_error("Unsupported input format: " + input_format);
    }
    if (data_source == "nuplan") {
        return load_nuplan_traj(input);
    }
    return load_simple_traj(input);
}

double degree_error_to_meters(double degree_error, const TrajectoryData& data) {
    if (!data.projected_from_degrees) {
        throw std::runtime_error("--max-error-degree requires lon/lat input. Use --input-format tdrive, or pass --max-error in meters.");
    }
    const double scale = std::max(data.meters_per_degree_latitude, data.meters_per_degree_longitude);
    return degree_error * scale;
}

class BitWriter {
public:
    void append_bit(bool bit) {
        bits_.push_back(bit ? 1u : 0u);
    }

    void append_bits_from_uint(std::uint64_t value, int bit_length, bool msb_first) {
        if (bit_length <= 0) {
            return;
        }
        if (msb_first) {
            for (int i = bit_length - 1; i >= 0; --i) {
                append_bit((value >> i) & 1ULL);
            }
        } else {
            for (int i = 0; i < bit_length; ++i) {
                append_bit((value >> i) & 1ULL);
            }
        }
    }

    void append_double(double value) {
        union {
            double d;
            std::uint8_t bytes[8];
        } u{};
        u.d = value;
        for (int byte = 0; byte < 8; ++byte) {
            append_bits_from_uint(u.bytes[byte], 8, false);
        }
    }

    void append_float(float value) {
        union {
            float f;
            std::uint8_t bytes[4];
        } u{};
        u.f = value;
        for (int byte = 0; byte < 4; ++byte) {
            append_bits_from_uint(u.bytes[byte], 8, false);
        }
    }

    void append_varint(std::uint64_t value, int data_length) {
        while (true) {
            const std::uint64_t data = value & ((1ULL << data_length) - 1ULL);
            value >>= data_length;
            const bool has_more = value > 0;
            append_bit(has_more);
            append_bits_from_uint(data, data_length, true);
            if (!has_more) {
                break;
            }
        }
    }

    void append_zigzag(std::int64_t value, int data_length) {
        std::uint64_t encoded = value < 0 ? static_cast<std::uint64_t>(-2 * value - 1) : static_cast<std::uint64_t>(2 * value);
        append_varint(encoded, data_length);
    }

    void append_signint(std::int64_t value, int bit_length) {
        if (bit_length <= 0) {
            return;
        }
        std::uint64_t encoded = 0;
        if (value < 0) {
            encoded = (1ULL << bit_length) + value;
        } else {
            encoded = static_cast<std::uint64_t>(value);
        }
        append_bits_from_uint(encoded, bit_length, true);
    }

    void append_customint(std::int64_t value) {
        if (value == 0) {
            append_bit(false);
            return;
        }
        if (value == 1) {
            append_bit(true);
            append_bit(false);
            append_bit(false);
            return;
        }
        if (value == -1) {
            append_bit(true);
            append_bit(false);
            append_bit(true);
            return;
        }

        const bool negative = value < 0;
        const std::uint64_t abs_value = static_cast<std::uint64_t>(negative ? -value : value);
        int k = 0;
        std::uint64_t tmp = abs_value;
        while (tmp > 0) {
            ++k;
            tmp >>= 1;
        }
        for (int i = 0; i < k; ++i) {
            append_bit(true);
        }
        append_bit(false);
        append_bit(negative);
        const std::uint64_t value_part = abs_value - (1ULL << (k - 1));
        append_bits_from_uint(value_part, k - 1, true);
    }

    void write_to_file(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Cannot write output file: " + path);
        }
        std::uint8_t current = 0;
        int bit_count = 0;
        for (std::uint8_t bit : bits_) {
            current = static_cast<std::uint8_t>((current << 1) | (bit & 1u));
            ++bit_count;
            if (bit_count == 8) {
                out.put(static_cast<char>(current));
                current = 0;
                bit_count = 0;
            }
        }
        if (bit_count != 0) {
            current <<= (8 - bit_count);
            out.put(static_cast<char>(current));
        }
    }

    std::size_t bit_count() const {
        return bits_.size();
    }

private:
    std::vector<std::uint8_t> bits_;
};

class BitReader {
public:
    explicit BitReader(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open compressed file: " + path);
        }
        std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        for (unsigned char byte : bytes) {
            for (int i = 7; i >= 0; --i) {
                bits_.push_back((byte >> i) & 1u);
            }
        }
    }

    bool read_bit() {
        if (index_ >= bits_.size()) {
            throw std::runtime_error("BitReader out of range");
        }
        return bits_[index_++] != 0;
    }

    std::uint64_t read_uint_bits(int bit_length, bool msb_first) {
        if (bit_length <= 0) {
            return 0;
        }
        std::uint64_t value = 0;
        if (msb_first) {
            for (int i = 0; i < bit_length; ++i) {
                value = (value << 1) | (read_bit() ? 1ULL : 0ULL);
            }
        } else {
            for (int i = 0; i < bit_length; ++i) {
                if (read_bit()) {
                    value |= (1ULL << i);
                }
            }
        }
        return value;
    }

    double read_double() {
        union {
            double d;
            std::uint8_t bytes[8];
        } u{};
        for (int byte = 0; byte < 8; ++byte) {
            u.bytes[byte] = static_cast<std::uint8_t>(read_uint_bits(8, false));
        }
        return u.d;
    }

    float read_float() {
        union {
            float f;
            std::uint8_t bytes[4];
        } u{};
        for (int byte = 0; byte < 4; ++byte) {
            u.bytes[byte] = static_cast<std::uint8_t>(read_uint_bits(8, false));
        }
        return u.f;
    }

    std::uint64_t read_varint(int data_length) {
        std::uint64_t result = 0;
        int shift = 0;
        while (true) {
            const bool has_more = read_bit();
            const std::uint64_t data = read_uint_bits(data_length, true);
            result |= (data << shift);
            shift += data_length;
            if (!has_more) {
                break;
            }
        }
        return result;
    }

    std::int64_t read_zigzag(int data_length) {
        const std::uint64_t value = read_varint(data_length);
        if ((value & 1ULL) == 0ULL) {
            return static_cast<std::int64_t>(value >> 1U);
        }
        return -static_cast<std::int64_t>((value + 1ULL) >> 1U);
    }

    std::int64_t read_signint(int bit_length, bool signed_value) {
        if (bit_length <= 0) {
            return 0;
        }
        const std::uint64_t value = read_uint_bits(bit_length, true);
        if (signed_value && ((value >> (bit_length - 1)) & 1ULL)) {
            return -static_cast<std::int64_t>((~value & ((1ULL << bit_length) - 1ULL)) + 1ULL);
        }
        return static_cast<std::int64_t>(value);
    }

    std::int64_t read_customint() {
        if (!read_bit()) {
            return 0;
        }
        int k = 1;
        while (peek_bit()) {
            read_bit();
            ++k;
        }
        read_bit();
        const bool negative = read_bit();
        const std::uint64_t tail = read_uint_bits(k - 1, true);
        std::int64_t value = static_cast<std::int64_t>((1ULL << (k - 1)) + tail);
        if (negative) {
            value = -value;
        }
        return value;
    }

    bool eof_with_padding() const {
        return index_ >= bits_.size() - 8;
    }

    std::size_t bit_count() const {
        return bits_.size();
    }

private:
    bool peek_bit() const {
        if (index_ >= bits_.size()) {
            throw std::runtime_error("BitReader out of range");
        }
        return bits_[index_] != 0;
    }

    std::vector<std::uint8_t> bits_;
    std::size_t index_ = 0;
};

double l2_norm(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum);
}

std::vector<double> vec_add(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] + b[i];
    }
    return out;
}

std::vector<double> vec_sub(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] - b[i];
    }
    return out;
}

std::vector<double> vec_scale(const std::vector<double>& a, double s) {
    std::vector<double> out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] * s;
    }
    return out;
}

std::vector<double> vec_lerp(const std::vector<double>& a, const std::vector<double>& b, double alpha) {
    std::vector<double> out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] + (b[i] - a[i]) * alpha;
    }
    return out;
}

std::vector<double> dct_type2_backward(const std::vector<double>& x) {
    const std::size_t n = x.size();
    std::vector<double> out(n, 0.0);
    const double factor = kPi / (2.0 * static_cast<double>(n));
    for (std::size_t k = 0; k < n; ++k) {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            sum += x[i] * std::cos((2.0 * static_cast<double>(i) + 1.0) * static_cast<double>(k) * factor);
        }
        out[k] = 2.0 * sum;
    }
    return out;
}

std::vector<double> idct_type2_backward(const std::vector<double>& c) {
    const std::size_t n = c.size();
    std::vector<double> out(n, 0.0);
    const double factor = kPi / (2.0 * static_cast<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        double sum = c[0];
        for (std::size_t k = 1; k < n; ++k) {
            sum += 2.0 * c[k] * std::cos((2.0 * static_cast<double>(i) + 1.0) * static_cast<double>(k) * factor);
        }
        out[i] = sum / (2.0 * static_cast<double>(n));
    }
    return out;
}

struct EncodedBlock {
    std::vector<std::int64_t> coeffs;
    double end_point = 0.0;
};

struct EncodedSegment {
    double start_t = 0.0;
    std::vector<double> start_point;
    int temp_block_size = 0;
    int delta_num = 0;
    std::vector<std::vector<EncodedBlock>> dims;
};

struct ErrorPoint {
    std::vector<std::int64_t> deltas;
    double t = 0.0;
};

class PILOTCReference {
public:
    PILOTCReference(TrajectoryData data, Config config)
        : data_(std::move(data)), config_(std::move(config)) {
        if (data_.traj.empty()) {
            throw std::runtime_error("Trajectory is empty");
        }
        traj_dim_ = static_cast<int>(data_.traj.front().size());
    }

    void compress(const std::string& output_path,
                  double max_error_in,
                  int utf_in,
                  double accuracy_in,
                  int block_size_in,
                  double scale_length_in,
                  double epsilon_in) {
        const double max_error = static_cast<double>(static_cast<float>(std::isnan(max_error_in) ? config_.error_default : max_error_in));
        const int utf = utf_in < 0 ? config_.utf_default : utf_in;
        const double accuracy = std::isnan(accuracy_in) ? config_.accuracy_default : accuracy_in;
        const int block_size = block_size_in < 0 ? static_cast<int>(max_error * config_.a + config_.b) : block_size_in;
        const double scale_length = std::isnan(scale_length_in) ? std::min(1.0, config_.c / std::sqrt(max_error)) : scale_length_in;
        const double scale = std::isnan(epsilon_in) ? (config_.scale_factor / max_error) : (0.5 / epsilon_in / max_error);
        const double max_accuracy_error = accuracy * max_error;
        const double max_error_dim = max_error / std::sqrt(static_cast<double>(traj_dim_));
        const double max_accuracy_error_dim = max_accuracy_error / std::sqrt(static_cast<double>(traj_dim_));

        auto [segments, other_points] = segment_trajectory(block_size);
        std::vector<EncodedSegment> result_list;
        std::vector<ErrorPoint> error_points;

        for (const auto& segment : segments) {
            result_list.push_back(encode_segment(segment.first, segment.second, block_size, scale_length, scale, max_accuracy_error_dim));
        }

        auto reconstructed = reconstruct_from_segments(result_list, other_points, scale, max_error_dim, max_accuracy_error_dim, max_error, error_points);
        (void)reconstructed;

        BitWriter writer;
        writer.append_varint(result_list.size(), utf);
        writer.append_double(config_.time_accuracy);
        writer.append_float(static_cast<float>(max_error));

        std::vector<double> last_value(traj_dim_, 0.0);
        for (std::size_t seg_idx = 0; seg_idx < result_list.size(); ++seg_idx) {
            const auto& seg = result_list[seg_idx];
            if (seg_idx == 0) {
                writer.append_varint(static_cast<std::uint64_t>(round_even(seg.start_t / config_.time_accuracy)), utf);
            } else {
                const double gap = seg.start_t - result_list[seg_idx - 1].start_t - (result_list[seg_idx - 1].delta_num - 1) * config_.time_unit;
                writer.append_varint(static_cast<std::uint64_t>(round_even(gap / config_.time_accuracy)), utf);
            }
            writer.append_varint(seg.delta_num, utf);

            for (int dim = 0; dim < traj_dim_; ++dim) {
                const auto current = static_cast<std::int64_t>(round_even(seg.start_point[dim] / max_accuracy_error_dim / 2.0));
                const auto prev = static_cast<std::int64_t>(round_even(last_value[dim] / max_accuracy_error_dim / 2.0));
                writer.append_zigzag(current - prev, utf);
                last_value[dim] = seg.start_point[dim];
            }

            const int block_count = seg.dims.empty() ? 0 : static_cast<int>(seg.dims[0].size());
            for (int block_idx = 0; block_idx < block_count; ++block_idx) {
                for (int dim = 0; dim < traj_dim_; ++dim) {
                    const auto current = static_cast<std::int64_t>(round_even(seg.dims[dim][block_idx].end_point / max_accuracy_error_dim / 2.0));
                    const auto prev = static_cast<std::int64_t>(round_even(last_value[dim] / max_accuracy_error_dim / 2.0));
                    writer.append_zigzag(current - prev, utf);
                    last_value[dim] = seg.dims[dim][block_idx].end_point;
                }
            }

            const int length_bit = std::max(0, static_cast<int>(std::ceil(std::log2(static_cast<int>((seg.temp_block_size - 1) * scale_length) + 1.0))));
            for (int dim = 0; dim < traj_dim_; ++dim) {
                for (const auto& block : seg.dims[dim]) {
                    writer.append_signint(static_cast<std::int64_t>(block.coeffs.size()), length_bit);
                    for (auto coeff : block.coeffs) {
                        writer.append_customint(coeff);
                    }
                }
            }
        }

        writer.append_varint(error_points.size(), utf);
        for (std::size_t i = 0; i < error_points.size(); ++i) {
            if (i == 0) {
                writer.append_varint(static_cast<std::uint64_t>(round_even(error_points[i].t / config_.time_accuracy)), utf);
            } else {
                writer.append_varint(static_cast<std::uint64_t>(round_even((error_points[i].t - error_points[i - 1].t) / config_.time_accuracy)), utf);
            }
            for (auto delta : error_points[i].deltas) {
                writer.append_customint(delta);
            }
        }

        for (std::size_t i = 0; i < other_points.size(); ++i) {
            const auto idx = other_points[i];
            if (i == 0) {
                writer.append_varint(static_cast<std::uint64_t>(round_even(data_.t_list[idx] / config_.time_accuracy)), utf);
            } else {
                writer.append_varint(static_cast<std::uint64_t>(round_even((data_.t_list[idx] - data_.t_list[other_points[i - 1]]) / config_.time_accuracy)), utf);
            }
            for (int dim = 0; dim < traj_dim_; ++dim) {
                const auto current = static_cast<std::int64_t>(round_even(data_.traj[idx][dim] / max_error_dim / 2.0));
                if (i == 0) {
                    writer.append_zigzag(current, utf);
                } else {
                    const auto prev = static_cast<std::int64_t>(round_even(data_.traj[other_points[i - 1]][dim] / max_error_dim / 2.0));
                    writer.append_zigzag(current - prev, utf);
                }
            }
        }

        writer.write_to_file(output_path);
        compress_bits_ = writer.bit_count();
    }

    void decompress(const std::string& input_path,
                    double /*max_error_in*/,
                    int utf_in,
                    double accuracy_in,
                    int block_size_in,
                    double scale_length_in,
                    double epsilon_in) {
        const int utf = utf_in < 0 ? config_.utf_default : utf_in;
        const double accuracy = std::isnan(accuracy_in) ? config_.accuracy_default : accuracy_in;
        BitReader reader(input_path);
        compress_bits_ = reader.bit_count();

        const auto num = static_cast<int>(reader.read_varint(utf));
        const double time_accuracy = reader.read_double();
        const double max_error = static_cast<double>(reader.read_float());
        const double max_accuracy_error = accuracy * max_error;
        const double max_error_dim = max_error / std::sqrt(static_cast<double>(traj_dim_));
        const double max_accuracy_error_dim = max_accuracy_error / std::sqrt(static_cast<double>(traj_dim_));
        const int block_size = block_size_in < 0 ? static_cast<int>(max_error * config_.a + config_.b) : block_size_in;
        const double scale_length = std::isnan(scale_length_in) ? std::min(1.0, config_.c / std::sqrt(max_error)) : scale_length_in;
        const double scale = std::isnan(epsilon_in) ? (config_.scale_factor / max_error) : (0.5 / epsilon_in / max_error);

        std::vector<EncodedSegment> result_list;
        std::vector<double> last_value(traj_dim_, 0.0);

        for (int seg_idx = 0; seg_idx < num; ++seg_idx) {
            double start_t_units = 0.0;
            if (result_list.empty()) {
                start_t_units = static_cast<double>(reader.read_varint(utf));
            } else {
                const auto delta_units = static_cast<double>(reader.read_varint(utf));
                start_t_units = result_list.back().start_t / time_accuracy + delta_units +
                    round_even((result_list.back().delta_num - 1) * config_.time_unit / config_.time_accuracy);
            }
            const auto delta_num = static_cast<int>(reader.read_varint(utf));
            const int temp_block_num = (delta_num - 1) / block_size + 1;
            const int temp_block_size = (delta_num - 1) / temp_block_num + 1;
            const int length_bit = std::max(0, static_cast<int>(std::ceil(std::log2(static_cast<int>((temp_block_size - 1) * scale_length) + 1.0))));

            std::vector<std::vector<double>> point_list;
            for (int block = 0; block < temp_block_num + 1; ++block) {
                std::vector<double> point;
                for (int dim = 0; dim < traj_dim_; ++dim) {
                    const auto traj_num = reader.read_zigzag(utf);
                    const double value = last_value[dim] + static_cast<double>(traj_num) * max_accuracy_error_dim * 2.0;
                    point.push_back(value);
                    last_value[dim] = value;
                }
                point_list.push_back(std::move(point));
            }

            std::vector<std::vector<EncodedBlock>> dims;
            for (int dim = 0; dim < traj_dim_; ++dim) {
                std::vector<EncodedBlock> blocks;
                for (int block = 0; block < temp_block_num; ++block) {
                    const auto coeff_count = static_cast<int>(reader.read_signint(length_bit, false));
                    EncodedBlock encoded;
                    for (int j = 0; j < coeff_count; ++j) {
                        encoded.coeffs.push_back(reader.read_customint());
                    }
                    encoded.end_point = point_list[block + 1][dim];
                    blocks.push_back(std::move(encoded));
                }
                dims.push_back(std::move(blocks));
            }

            EncodedSegment seg;
            seg.start_t = start_t_units * time_accuracy;
            seg.start_point = point_list[0];
            seg.temp_block_size = temp_block_size;
            seg.delta_num = delta_num;
            seg.dims = std::move(dims);
            result_list.push_back(std::move(seg));
        }

        std::vector<ErrorPoint> error_points;
        const auto error_points_num = static_cast<int>(reader.read_varint(utf));
        for (int i = 0; i < error_points_num; ++i) {
            ErrorPoint point;
            double t_units = 0.0;
            if (i == 0) {
                t_units = static_cast<double>(reader.read_varint(utf));
            } else {
                t_units = error_points.back().t / time_accuracy + static_cast<double>(reader.read_varint(utf));
            }
            point.t = t_units * time_accuracy;
            for (int dim = 0; dim < traj_dim_; ++dim) {
                point.deltas.push_back(reader.read_customint());
            }
            error_points.push_back(std::move(point));
        }

        std::vector<std::pair<double, std::vector<double>>> other_points;
        while (!reader.eof_with_padding()) {
            double t_units = 0.0;
            if (other_points.empty()) {
                t_units = static_cast<double>(reader.read_varint(utf));
            } else {
                t_units = other_points.back().first / time_accuracy + static_cast<double>(reader.read_varint(utf));
            }
            std::vector<double> point;
            for (int dim = 0; dim < traj_dim_; ++dim) {
                if (other_points.empty()) {
                    point.push_back(static_cast<double>(reader.read_zigzag(utf)) * max_error_dim * 2.0);
                } else {
                    point.push_back(other_points.back().second[dim] + static_cast<double>(reader.read_zigzag(utf)) * max_error_dim * 2.0);
                }
            }
            other_points.push_back({t_units * time_accuracy, std::move(point)});
        }

        decompress_traj_ = reconstruct_for_decode(result_list, other_points, error_points, scale, max_error_dim);
    }

    Metrics evaluate(double max_error) const {
        if (decompress_traj_.size() != data_.traj.size()) {
            throw std::runtime_error("Decompressed trajectory length mismatch");
        }
        Metrics metrics;
        metrics.length = data_.traj.size();
        double error_sum = 0.0;
        std::vector<double> errors;
        for (std::size_t i = 0; i < data_.traj.size(); ++i) {
            const auto err = l2_norm(vec_sub(data_.traj[i], decompress_traj_[i]));
            errors.push_back(err);
            error_sum += err;
            metrics.max_error = std::max(metrics.max_error, err);
            if (err > max_error) {
                ++metrics.error_cnt;
            }
        }
        metrics.mean_error = error_sum / static_cast<double>(errors.size());
        double var = 0.0;
        for (double err : errors) {
            const double diff = err - metrics.mean_error;
            var += diff * diff;
        }
        metrics.var_error = var / static_cast<double>(errors.size());
        metrics.compress_ratio = (static_cast<double>(data_.traj.size()) * (static_cast<double>(traj_dim_) + 1.0) * 8.0) /
            static_cast<double>(compress_bits_) * 8.0;
        return metrics;
    }

    const std::vector<std::vector<double>>& decompressed() const {
        return decompress_traj_;
    }

private:
    std::pair<std::vector<std::pair<int, int>>, std::vector<int>> segment_trajectory(int block_size) const {
        std::vector<std::pair<int, int>> segments;
        std::vector<int> other_points;
        int start = 0;
        while (start < static_cast<int>(data_.t_list.size())) {
            int end = start;
            while (end < static_cast<int>(data_.t_list.size()) - 1) {
                const double dt = data_.t_list[end + 1] - data_.t_list[end];
                const double adaptive = (end != start) ? ((data_.t_list[end] - data_.t_list[start]) / static_cast<double>(end - start + 1)) : 1.0;
                const double speed = l2_norm(vec_sub(data_.traj[end + 1], data_.traj[end])) / dt;
                if (!(dt < block_size * adaptive && speed < 200.0)) {
                    break;
                }
                ++end;
            }
            if (end - start <= 1) {
                for (int i = start; i <= end; ++i) {
                    other_points.push_back(i);
                }
            } else {
                segments.push_back({start, end});
            }
            start = end + 1;
        }
        return {segments, other_points};
    }

    EncodedSegment encode_segment(int start_index,
                                  int end_index,
                                  int block_size,
                                  double scale_length,
                                  double scale,
                                  double max_accuracy_error_dim) const {
        std::vector<std::vector<double>> temp_traj;
        const double start_t = data_.t_list[start_index];
        for (int i = start_index; i < end_index; ++i) {
            const int min_t = static_cast<int>(std::ceil((data_.t_list[i] - start_t) / config_.time_unit));
            const int max_t = static_cast<int>(std::ceil((data_.t_list[i + 1] - start_t) / config_.time_unit)) + ((i == end_index - 1) ? 1 : 0);
            for (int j = min_t; j < max_t; ++j) {
                const double temp_t = start_t + static_cast<double>(j) * config_.time_unit;
                const double alpha = (temp_t - data_.t_list[i]) / (data_.t_list[i + 1] - data_.t_list[i]);
                temp_traj.push_back(vec_lerp(data_.traj[i], data_.traj[i + 1], alpha));
            }
        }

        const int temp_block_num = (static_cast<int>(temp_traj.size()) - 2) / block_size + 1;
        const int temp_block_size = (static_cast<int>(temp_traj.size()) - 2) / temp_block_num + 1;
        for (int block_id = 0; block_id < temp_block_num + 1; ++block_id) {
            const int temp_index = std::min(block_id * temp_block_size, static_cast<int>(temp_traj.size()) - 1);
            for (int dim = 0; dim < traj_dim_; ++dim) {
                temp_traj[temp_index][dim] = round_even(temp_traj[temp_index][dim] / max_accuracy_error_dim / 2.0) * max_accuracy_error_dim * 2.0;
            }
        }

        std::vector<std::vector<double>> delta;
        for (std::size_t i = 1; i < temp_traj.size(); ++i) {
            delta.push_back(vec_sub(temp_traj[i], temp_traj[i - 1]));
        }

        EncodedSegment segment;
        segment.start_t = start_t;
        segment.start_point = temp_traj[0];
        segment.temp_block_size = temp_block_size;
        segment.delta_num = static_cast<int>(delta.size());
        segment.dims.assign(traj_dim_, {});

        for (int dim = 0; dim < traj_dim_; ++dim) {
            for (int block_id = 0; block_id < temp_block_num; ++block_id) {
                const int left = block_id * temp_block_size;
                const int right = std::min((block_id + 1) * temp_block_size, static_cast<int>(delta.size()));
                std::vector<double> block_values;
                for (int idx = left; idx < right; ++idx) {
                    block_values.push_back(delta[idx][dim]);
                }
                const double mean = std::accumulate(block_values.begin(), block_values.end(), 0.0) / static_cast<double>(block_values.size());
                for (double& value : block_values) {
                    value -= mean;
                }
                auto dct = dct_type2_backward(block_values);
                int index = static_cast<int>((static_cast<int>(dct.size()) - 1) * scale_length);
                while (index > 0 && static_cast<int>(round_even(dct[index] * scale)) == 0) {
                    --index;
                }
                EncodedBlock block;
                for (int j = 1; j <= index; ++j) {
                    block.coeffs.push_back(static_cast<std::int64_t>(round_even(dct[j] * scale)));
                }
                block.end_point = temp_traj[std::min((block_id + 1) * temp_block_size, static_cast<int>(temp_traj.size()) - 1)][dim];
                segment.dims[dim].push_back(std::move(block));
            }
        }
        return segment;
    }

    std::vector<std::vector<double>> reconstruct_from_segments(const std::vector<EncodedSegment>& result_list,
                                                               const std::vector<int>& other_points,
                                                               double scale,
                                                               double max_error_dim,
                                                               double /*max_accuracy_error_dim*/,
                                                               double max_error,
                                                               std::vector<ErrorPoint>& error_points) const {
        auto all = reconstruct_segments_only(result_list, scale);
        auto merged = merge_with_other_points(all.first, all.second, other_points);
        for (std::size_t i = 0; i < merged.size(); ++i) {
            const auto err_vec = vec_sub(data_.traj[i], merged[i]);
            if (l2_norm(err_vec) > max_error) {
                ErrorPoint point;
                for (int dim = 0; dim < traj_dim_; ++dim) {
                    const auto value = static_cast<std::int64_t>(round_even(err_vec[dim] / max_error_dim / 2.0));
                    point.deltas.push_back(value);
                    merged[i][dim] += static_cast<double>(value) * max_error_dim * 2.0;
                }
                point.t = data_.t_list[i];
                error_points.push_back(std::move(point));
            }
        }
        return merged;
    }

    std::pair<std::vector<std::vector<double>>, std::vector<double>> reconstruct_segments_only(const std::vector<EncodedSegment>& result_list,
                                                                                                double scale) const {
        std::vector<std::vector<double>> decompress_traj0;
        std::vector<double> t_list0;
        for (const auto& seg : result_list) {
            std::vector<std::vector<double>> per_dim(traj_dim_);
            for (int dim = 0; dim < traj_dim_; ++dim) {
                for (std::size_t block_idx = 0; block_idx < seg.dims[dim].size(); ++block_idx) {
                    const auto& block = seg.dims[dim][block_idx];
                    const int block_len = std::min(seg.temp_block_size, seg.delta_num - seg.temp_block_size * static_cast<int>(block_idx));
                    const double prev = (block_idx > 0) ? seg.dims[dim][block_idx - 1].end_point : seg.start_point[dim];
                    const double mean = (block.end_point - prev) / static_cast<double>(block_len);
                    std::vector<double> coeffs(block_len, 0.0);
                    for (std::size_t i = 0; i < block.coeffs.size(); ++i) {
                        coeffs[i + 1] = static_cast<double>(block.coeffs[i]) / scale;
                    }
                    auto idct = idct_type2_backward(coeffs);
                    for (double& value : idct) {
                        value += mean;
                    }
                    per_dim[dim].insert(per_dim[dim].end(), idct.begin(), idct.end());
                }
            }

            if (!t_list0.empty() && seg.start_t <= t_list0.back()) {
                t_list0.pop_back();
                decompress_traj0.pop_back();
            }
            decompress_traj0.push_back(seg.start_point);
            t_list0.push_back(seg.start_t);

            const std::size_t step_count = per_dim[0].size();
            for (std::size_t i = 0; i < step_count; ++i) {
                std::vector<double> delta(traj_dim_, 0.0);
                for (int dim = 0; dim < traj_dim_; ++dim) {
                    delta[dim] = per_dim[dim][i];
                }
                decompress_traj0.push_back(vec_add(decompress_traj0.back(), delta));
                t_list0.push_back(t_list0.back() + config_.time_unit);
            }
        }
        return {decompress_traj0, t_list0};
    }

    std::vector<std::vector<double>> merge_with_other_points(const std::vector<std::vector<double>>& decompress_traj0,
                                                             const std::vector<double>& t_list0,
                                                             const std::vector<int>& other_points) const {
        std::vector<std::vector<double>> decompress_traj1;
        std::vector<double> t_list1;
        for (int idx : other_points) {
            decompress_traj1.push_back(data_.traj[idx]);
            t_list1.push_back(data_.t_list[idx]);
        }

        if (t_list0.empty()) {
            return decompress_traj1;
        }

        std::vector<std::vector<double>> merged;
        std::size_t index0 = 0;
        std::size_t index1 = 0;
        for (std::size_t i = 0; i < data_.t_list.size(); ++i) {
            const double t = data_.t_list[i];
            while (index0 < t_list0.size() - 1 && t_list0[index0 + 1] - t < -0.1 * config_.time_accuracy) {
                ++index0;
            }
            while (index1 < t_list1.size() && t_list1[index1] - t < -0.1 * config_.time_accuracy) {
                ++index1;
            }

            std::vector<double> temp_point;
            if (index0 == t_list0.size() - 1) {
                temp_point = decompress_traj0[index0];
            } else {
                const double alpha = (t - t_list0[index0]) / (t_list0[index0 + 1] - t_list0[index0]);
                temp_point = vec_lerp(decompress_traj0[index0], decompress_traj0[index0 + 1], alpha);
            }

            if (index1 < t_list1.size() && std::abs(t_list1[index1] - t) < 0.1 * config_.time_accuracy) {
                merged.push_back(decompress_traj1[index1]);
            } else {
                merged.push_back(temp_point);
            }
        }
        return merged;
    }

    std::vector<std::vector<double>> reconstruct_for_decode(const std::vector<EncodedSegment>& result_list,
                                                            const std::vector<std::pair<double, std::vector<double>>>& other_points,
                                                            const std::vector<ErrorPoint>& error_points,
                                                            double scale,
                                                            double max_error_dim) const {
        auto all = reconstruct_segments_only(result_list, scale);
        std::vector<std::vector<double>> decompress_traj1;
        std::vector<double> t_list1;
        for (const auto& item : other_points) {
            t_list1.push_back(item.first);
            decompress_traj1.push_back(item.second);
        }
        if (all.second.empty()) {
            return decompress_traj1;
        }

        std::vector<std::vector<double>> result;
        std::size_t index0 = 0;
        std::size_t index1 = 0;
        std::size_t index2 = 0;
        for (std::size_t i = 0; i < data_.t_list.size(); ++i) {
            const double t = data_.t_list[i];
            while (index0 < all.second.size() - 1 && all.second[index0 + 1] - t < -0.1 * config_.time_accuracy) {
                ++index0;
            }
            while (index1 < t_list1.size() && t_list1[index1] - t < -0.1 * config_.time_accuracy) {
                ++index1;
            }
            while (index2 < error_points.size() && error_points[index2].t - t < -0.1 * config_.time_accuracy) {
                ++index2;
            }

            std::vector<double> point;
            if (index0 == all.second.size() - 1) {
                point = all.first[index0];
            } else {
                const double alpha = (t - all.second[index0]) / (all.second[index0 + 1] - all.second[index0]);
                point = vec_lerp(all.first[index0], all.first[index0 + 1], alpha);
            }

            if (index1 < t_list1.size() && std::abs(t_list1[index1] - t) < 0.1 * config_.time_accuracy) {
                point = decompress_traj1[index1];
            }
            if (index2 < error_points.size() && std::abs(error_points[index2].t - t) < 0.1 * config_.time_accuracy) {
                for (int dim = 0; dim < traj_dim_; ++dim) {
                    point[dim] += static_cast<double>(error_points[index2].deltas[dim]) * max_error_dim * 2.0;
                }
            }
            result.push_back(std::move(point));
        }
        return result;
    }

    TrajectoryData data_;
    Config config_;
    int traj_dim_ = 0;
    std::size_t compress_bits_ = 0;
    std::vector<std::vector<double>> decompress_traj_;
};

struct CliArgs {
    std::string input;
    std::string input_format = "simple";
    std::string data_source = "test_scalability";
    std::string output = "pilotc_compressed.bin";
    double max_error = std::numeric_limits<double>::quiet_NaN();
    double max_error_degree = std::numeric_limits<double>::quiet_NaN();
    double a = std::numeric_limits<double>::quiet_NaN();
    double b = std::numeric_limits<double>::quiet_NaN();
    double c = std::numeric_limits<double>::quiet_NaN();
    int utf = -1;
    double accuracy = std::numeric_limits<double>::quiet_NaN();
    int block_size = -1;
    double scale_length = std::numeric_limits<double>::quiet_NaN();
    double epsilon = std::numeric_limits<double>::quiet_NaN();
};

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };
        if (key == "--input") {
            args.input = require_value(key);
        } else if (key == "--input-format") {
            args.input_format = require_value(key);
        } else if (key == "--data-source") {
            args.data_source = require_value(key);
        } else if (key == "--output") {
            args.output = require_value(key);
        } else if (key == "--max-error") {
            args.max_error = std::stod(require_value(key));
        } else if (key == "--max-error-degree") {
            args.max_error_degree = std::stod(require_value(key));
        } else if (key == "--a") {
            args.a = std::stod(require_value(key));
        } else if (key == "--b") {
            args.b = std::stod(require_value(key));
        } else if (key == "--c") {
            args.c = std::stod(require_value(key));
        } else if (key == "--utf") {
            args.utf = std::stoi(require_value(key));
        } else if (key == "--accuracy") {
            args.accuracy = std::stod(require_value(key));
        } else if (key == "--block-size") {
            args.block_size = std::stoi(require_value(key));
        } else if (key == "--scale-length") {
            args.scale_length = std::stod(require_value(key));
        } else if (key == "--epsilon") {
            args.epsilon = std::stod(require_value(key));
        } else {
            throw std::runtime_error("Unknown argument: " + key);
        }
    }
    if (args.input.empty()) {
        throw std::runtime_error("Usage: pilotc_reference --input <path> [--data-source test_scalability]");
    }
    return args;
}

}  // namespace pilotc

int main(int argc, char** argv) {
    try {
        const auto args = pilotc::parse_args(argc, argv);
        const auto data = pilotc::load_trajectory(args.input, args.data_source, args.input_format);
        auto config = pilotc::make_config(args.data_source);
        if (!std::isnan(args.a)) {
            config.a = args.a;
        }
        if (!std::isnan(args.b)) {
            config.b = args.b;
        }
        if (!std::isnan(args.c)) {
            config.c = args.c;
        }
        double max_error = args.max_error;
        if (!std::isnan(args.max_error_degree)) {
            if (!std::isnan(args.max_error)) {
                throw std::runtime_error("Use either --max-error or --max-error-degree, not both.");
            }
            max_error = pilotc::degree_error_to_meters(args.max_error_degree, data);
        }

        pilotc::PILOTCReference compressor(data, config);
        const auto pipeline_start = std::chrono::steady_clock::now();
        const auto compress_start = std::chrono::steady_clock::now();
        compressor.compress(args.output, max_error, args.utf, args.accuracy, args.block_size, args.scale_length, args.epsilon);
        const auto compress_end = std::chrono::steady_clock::now();
        compressor.decompress(args.output, max_error, args.utf, args.accuracy, args.block_size, args.scale_length, args.epsilon);
        const auto decompress_end = std::chrono::steady_clock::now();

        const double metric_error = std::isnan(max_error) ? config.error_default : max_error;
        auto metrics = compressor.evaluate(metric_error);
        metrics.compress_time_ms = std::chrono::duration<double, std::milli>(compress_end - compress_start).count();
        metrics.decompress_time_ms = std::chrono::duration<double, std::milli>(decompress_end - compress_end).count();
        metrics.pipeline_time_ms = std::chrono::duration<double, std::milli>(decompress_end - pipeline_start).count();

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "{\n";
        std::cout << "  \"max_error\": " << metrics.max_error << ",\n";
        std::cout << "  \"mean_error\": " << metrics.mean_error << ",\n";
        std::cout << "  \"compress_ratio\": " << metrics.compress_ratio << ",\n";
        std::cout << "  \"length\": " << metrics.length << ",\n";
        std::cout << "  \"error_cnt\": " << metrics.error_cnt << ",\n";
        std::cout << "  \"var_error\": " << metrics.var_error << ",\n";
        std::cout << "  \"compress_time_ms\": " << metrics.compress_time_ms << ",\n";
        std::cout << "  \"decompress_time_ms\": " << metrics.decompress_time_ms << ",\n";
        std::cout << "  \"pipeline_time_ms\": " << metrics.pipeline_time_ms << "\n";
        std::cout << "}\n";
        std::cout << "config_a=" << config.a << "\n";
        std::cout << "config_b=" << config.b << "\n";
        std::cout << "config_c=" << config.c << "\n";
        if (!std::isnan(args.max_error_degree)) {
            std::cout << "degree_threshold=" << args.max_error_degree << "\n";
            std::cout << "meter_threshold=" << max_error << "\n";
            std::cout << "meters_per_degree_latitude=" << data.meters_per_degree_latitude << "\n";
            std::cout << "meters_per_degree_longitude=" << data.meters_per_degree_longitude << "\n";
            std::cout << "reference_latitude_degrees=" << data.reference_latitude_degrees << "\n";
        }
        std::cout << "compressed_file=" << args.output << "\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
    return 0;
}
