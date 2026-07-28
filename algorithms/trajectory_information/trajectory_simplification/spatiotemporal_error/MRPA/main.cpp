#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct RawPoint {
    double lat;
    double lon;
    double unix_time;
};

struct ProjectedData {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> t;
    double scale_factor;
};

struct PrefixSums {
    std::vector<double> x_sum;
    std::vector<double> y_sum;
    std::vector<double> t_sum;
    std::vector<double> x2_sum;
    std::vector<double> y2_sum;
    std::vector<double> t2_sum;
    std::vector<double> xt_sum;
    std::vector<double> yt_sum;
};

struct InitResult {
    std::vector<std::vector<int>> linkcell;
    std::vector<int> parent;
};

struct SimplifyResult {
    std::vector<int> path;
};

double deg2rad(double deg) {
    return deg * kPi / 180.0;
}

bool nearly_equal(double a, double b, double eps = 1e-12) {
    return std::fabs(a - b) <= eps;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        parts.push_back(token);
    }
    return parts;
}

std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3U : 9U)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(doe) - 719468;
}

double unix_time_from_parts(int year, int month, int day, int hour, int minute, int second) {
    const std::int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return static_cast<double>(days * 86400 + hour * 3600 + minute * 60 + second);
}

std::tuple<int, int, int> parse_date(const std::string& value) {
    int y = 0;
    int m = 0;
    int d = 0;
    char dash1 = '\0';
    char dash2 = '\0';
    std::stringstream ss(value);
    ss >> y >> dash1 >> m >> dash2 >> d;
    if (!ss || dash1 != '-' || dash2 != '-') {
        throw std::runtime_error("invalid date: " + value);
    }
    return {y, m, d};
}

std::tuple<int, int, int> parse_time(const std::string& value) {
    int h = 0;
    int m = 0;
    int s = 0;
    char colon1 = '\0';
    char colon2 = '\0';
    std::stringstream ss(value);
    ss >> h >> colon1 >> m >> colon2 >> s;
    if (!ss || colon1 != ':' || colon2 != ':') {
        throw std::runtime_error("invalid time: " + value);
    }
    return {h, m, s};
}

std::vector<RawPoint> read_geolife(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + path);
    }

    std::vector<RawPoint> points;
    std::string line;
    int line_no = 0;
    while (std::getline(input, line)) {
        ++line_no;
        if (line_no <= 6 || line.empty()) {
            continue;
        }
        const auto parts = split_csv(line);
        if (parts.size() < 7) {
            continue;
        }

        const double lat = std::stod(parts[0]);
        const double lon = std::stod(parts[1]);
        const auto [year, month, day] = parse_date(parts[5]);
        const auto [hour, minute, second] = parse_time(parts[6]);
        const double unix_time = unix_time_from_parts(year, month, day, hour, minute, second);

        if (!points.empty() && nearly_equal(points.back().unix_time, unix_time)) {
            points.back() = RawPoint{lat, lon, unix_time};
            continue;
        }

        points.push_back(RawPoint{lat, lon, unix_time});
    }

    if (points.size() < 2) {
        throw std::runtime_error("not enough valid points in: " + path);
    }
    return points;
}

ProjectedData mercator_project(const std::vector<RawPoint>& raw) {
    constexpr double xy_dist = 6378100.0;
    constexpr double acc_scale_factor = 1e-4;
    constexpr double acc_x = 0.1;
    constexpr double acc_y = 0.1;

    std::vector<double> sf;
    sf.reserve(raw.size());
    for (const auto& point : raw) {
        const double y = deg2rad(point.lat);
        sf.push_back(1.0 / std::cos(y));
    }
    const double mean_sf = std::accumulate(sf.begin(), sf.end(), 0.0) / static_cast<double>(sf.size());
    const double scale_factor = std::round(mean_sf / acc_scale_factor) * acc_scale_factor;

    ProjectedData projected;
    projected.x.resize(raw.size() + 1);
    projected.y.resize(raw.size() + 1);
    projected.t.resize(raw.size() + 1);
    projected.scale_factor = scale_factor;

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const double x = deg2rad(raw[i].lon);
        const double y = deg2rad(raw[i].lat);
        const double y2 = std::log(std::abs(std::tan(y) + 1.0 / std::cos(y)));

        projected.x[i + 1] = std::round((x * xy_dist / scale_factor) / acc_x) * acc_x;
        projected.y[i + 1] = std::round((y2 * xy_dist / scale_factor) / acc_y) * acc_y;
        projected.t[i + 1] = raw[i].unix_time;
    }
    return projected;
}

PrefixSums build_prefix_sums(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& t) {
    const int n = static_cast<int>(x.size()) - 1;
    PrefixSums sums;
    sums.x_sum.assign(n + 1, 0.0);
    sums.y_sum.assign(n + 1, 0.0);
    sums.t_sum.assign(n + 1, 0.0);
    sums.x2_sum.assign(n + 1, 0.0);
    sums.y2_sum.assign(n + 1, 0.0);
    sums.t2_sum.assign(n + 1, 0.0);
    sums.xt_sum.assign(n + 1, 0.0);
    sums.yt_sum.assign(n + 1, 0.0);

    for (int i = 1; i <= n; ++i) {
        sums.x_sum[i] = sums.x_sum[i - 1] + x[i];
        sums.y_sum[i] = sums.y_sum[i - 1] + y[i];
        sums.t_sum[i] = sums.t_sum[i - 1] + t[i];
        sums.x2_sum[i] = sums.x2_sum[i - 1] + x[i] * x[i];
        sums.y2_sum[i] = sums.y2_sum[i - 1] + y[i] * y[i];
        sums.t2_sum[i] = sums.t2_sum[i - 1] + t[i] * t[i];
        sums.xt_sum[i] = sums.xt_sum[i - 1] + x[i] * t[i];
        sums.yt_sum[i] = sums.yt_sum[i - 1] + y[i] * t[i];
    }
    return sums;
}

double segment_sed_sum(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& t,
    const PrefixSums& sums,
    int idx_i,
    int idx_j,
    int orig_i,
    int orig_j) {
    if (orig_i + 1 == orig_j) {
        return 0.0;
    }

    const double xi = x[idx_i];
    const double yi = y[idx_i];
    const double ti = t[idx_i];
    const double xj = x[idx_j];
    const double yj = y[idx_j];
    const double tj = t[idx_j];

    const double c1x = xi * tj - xj * ti;
    const double c2x = c1x * c1x;
    const double c3x = tj - ti;
    const double c4x = c3x * c3x;
    const double c5x = xj - xi;
    const double c6x = c5x * c5x;

    double dist = static_cast<double>(orig_j - orig_i - 1) * c2x / c4x
        + c6x / c4x * (sums.t2_sum[orig_j - 1] - sums.t2_sum[orig_i])
        + (sums.x2_sum[orig_j - 1] - sums.x2_sum[orig_i])
        + 2.0 * c1x * c5x / c4x * (sums.t_sum[orig_j - 1] - sums.t_sum[orig_i])
        - 2.0 * c1x / c3x * (sums.x_sum[orig_j - 1] - sums.x_sum[orig_i])
        - 2.0 * c5x / c3x * (sums.xt_sum[orig_j - 1] - sums.xt_sum[orig_i]);

    const double c1y = yi * tj - yj * ti;
    const double c2y = c1y * c1y;
    const double c3y = c3x;
    const double c4y = c3y * c3y;
    const double c5y = yj - yi;
    const double c6y = c5y * c5y;

    dist += static_cast<double>(orig_j - orig_i - 1) * c2y / c4y
        + c6y / c4y * (sums.t2_sum[orig_j - 1] - sums.t2_sum[orig_i])
        + (sums.y2_sum[orig_j - 1] - sums.y2_sum[orig_i])
        + 2.0 * c1y * c5y / c4y * (sums.t_sum[orig_j - 1] - sums.t_sum[orig_i])
        - 2.0 * c1y / c3y * (sums.y_sum[orig_j - 1] - sums.y_sum[orig_i])
        - 2.0 * c5y / c3y * (sums.yt_sum[orig_j - 1] - sums.yt_sum[orig_i]);

    return dist;
}

double calc_issed_pathpt(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& t,
    const std::vector<int>& path,
    const PrefixSums& sums) {
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        total += segment_sed_sum(x, y, t, sums, path[i], path[i + 1], path[i], path[i + 1]);
    }
    return total;
}

InitResult pa_min_nsed_init(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& t,
    const PrefixSums& sums,
    double threshold,
    double next_threshold,
    const std::vector<int>& ptind) {
    const int n = static_cast<int>(x.size()) - 1;
    InitResult result;
    result.parent.assign(n + 1, -1);

    std::vector<int> segind(n + 1, std::numeric_limits<int>::max());
    std::vector<bool> unvisited(n + 1, true);
    std::vector<int> q1(n + 1, 1);
    std::vector<int> q2(n + 1, -1);
    std::vector<int> q2temp(n + 1, -1);

    int q1_size = 1;
    int q2_pos = 1;
    int current_len = 0;

    result.linkcell.push_back({});
    result.linkcell.push_back({1});
    segind[1] = 0;
    unvisited[1] = false;

    while (segind[n] == std::numeric_limits<int>::max()) {
        for (int p1 = 1; p1 <= q1_size; ++p1) {
            const int j = q1[p1];
            int q2t_pos = 1;
            for (int i = j + 1; i <= n; ++i) {
                if (!unvisited[i]) {
                    continue;
                }
                const int lst = ptind[i];
                const int fst = ptind[j];
                const double dist = segment_sed_sum(x, y, t, sums, j, i, fst, lst);
                if (dist > next_threshold) {
                    break;
                }
                if (dist <= threshold) {
                    q2temp[q2t_pos++] = i;
                    unvisited[i] = false;
                    result.parent[i] = j;
                }
            }
            const int tmp = q2t_pos - 1;
            const int end = q2_pos + tmp - 1;
            for (int k = 0; k < tmp; ++k) {
                q2[q2_pos + k] = q2temp[1 + k];
            }
            q2_pos = end + 1;
        }

        const int tmp = q2_pos - 1;
        if (tmp <= 0) {
            throw std::runtime_error("MRPA init failed to reach the end point");
        }

        std::sort(q2.begin() + 1, q2.begin() + tmp + 1, std::greater<int>());
        ++current_len;

        result.linkcell.push_back({});
        for (int k = 1; k <= tmp; ++k) {
            segind[q2[k]] = current_len;
            result.linkcell.back().push_back(q2[k]);
            q1[k] = q2[k];
        }
        q1_size = tmp;
        q2_pos = 1;
    }

    return result;
}

std::vector<std::vector<int>> pa_min_msed_tree(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& t,
    std::vector<std::vector<int>> linkcell,
    std::vector<int>& parent,
    const PrefixSums& sums,
    int w,
    double threshold,
    const std::vector<int>& ptind) {
    const int n = static_cast<int>(x.size()) - 1;
    const int pathnum = static_cast<int>(linkcell.size()) - 1;
    if (pathnum < 3) {
        return linkcell;
    }

    for (int i = pathnum; i >= 3; --i) {
        const std::vector<int> tarpt = linkcell[i];
        std::vector<bool> keep(tarpt.size(), true);

        for (int width = w; width >= 1; --width) {
            const int tmp = i - 1 - width;
            if (tmp <= 0) {
                continue;
            }

            std::vector<int> movpt;
            const std::vector<int> curpt = linkcell[tmp];

            for (std::size_t tar_idx = 0; tar_idx < tarpt.size(); ++tar_idx) {
                if (!keep[tar_idx]) {
                    continue;
                }
                const int ind2 = tarpt[tar_idx];
                for (const int ind1 : curpt) {
                    if (ind2 <= ind1) {
                        continue;
                    }
                    const double dist = segment_sed_sum(x, y, t, sums, ind1, ind2, ptind[ind1], ptind[ind2]);
                    if (dist <= threshold) {
                        keep[tar_idx] = false;
                        parent[ind2] = ind1;
                        movpt.push_back(ind2);
                        break;
                    }
                }
            }

            std::vector<int> filtered;
            for (std::size_t idx = 0; idx < tarpt.size(); ++idx) {
                if (keep[idx]) {
                    filtered.push_back(tarpt[idx]);
                }
            }
            linkcell[i] = filtered;

            if (!movpt.empty()) {
                auto& destination = linkcell[tmp + 1];
                destination.insert(destination.end(), movpt.begin(), movpt.end());
            }
        }
    }

    std::vector<int> segind(n + 1, 1);
    for (int i = 2; i <= n; ++i) {
        const int par = parent[i];
        if (par < n && par > 0) {
            segind[i] = segind[par] + 1;
        } else {
            segind[i] = std::numeric_limits<int>::max();
        }
    }

    const int nlink = segind[n];
    if (nlink < pathnum) {
        std::vector<std::vector<int>> relink(nlink + 1);
        relink[1] = {1};
        std::vector<int> freq(nlink + 1, 0);
        for (int i = 1; i <= n; ++i) {
            if (segind[i] <= nlink) {
                ++freq[segind[i]];
            }
        }
        for (int level = 2; level <= nlink; ++level) {
            relink[level].reserve(freq[level]);
        }
        for (int i = 2; i <= n; ++i) {
            const int level = segind[i];
            if (level <= nlink) {
                relink[level].push_back(i);
            }
        }
        return relink;
    }

    return linkcell;
}

SimplifyResult pa_mine_sed_crsdp(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& t,
    std::vector<std::vector<int>> linkcell,
    const PrefixSums& sums,
    double threshold,
    const std::vector<int>& ptind) {
    const int n = static_cast<int>(x.size()) - 1;
    const int pathnum = static_cast<int>(linkcell.size()) - 2;
    std::vector<double> energy(n + 1, std::numeric_limits<double>::infinity());
    std::vector<int> parent(n + 1, -1);
    energy[1] = 0.0;
    parent[1] = 0;
    linkcell.back() = {n};

    for (int m = 1; m <= pathnum; ++m) {
        const auto& tarind = linkcell[m + 1];
        const auto& curind = linkcell[m];
        for (const int node_n : tarind) {
            double best = std::numeric_limits<double>::infinity();
            int best_parent = -1;
            for (const int node_j : curind) {
                if (node_j >= node_n) {
                    continue;
                }
                const double dist = segment_sed_sum(x, y, t, sums, node_j, node_n, ptind[node_j], ptind[node_n]);
                if (dist <= threshold) {
                    const double candidate = energy[node_j] + dist;
                    if (candidate < best) {
                        best = candidate;
                        best_parent = node_j;
                    }
                }
            }
            energy[node_n] = best;
            parent[node_n] = best_parent;
        }
    }

    // MATLAB allocates pathpt with pathnum+1 entries: the endpoint is
    // followed through exactly pathnum parent links back to the root.
    // The previous port allocated one extra entry and forced path[0]=1,
    // which inserted a duplicate/misaligned node in every intermediate
    // multiresolution curve.
    std::vector<int> path(pathnum + 1, 0);
    path[pathnum] = n;
    for (int i = pathnum - 1; i >= 0; --i) {
        path[i] = parent[path[i + 1]];
    }

    SimplifyResult result;
    result.path = path;
    return result;
}

std::vector<int> tdmrpa_sed(
    std::vector<double> x,
    std::vector<double> y,
    std::vector<double> t,
    double lissed_threshold,
    int c = 2,
    int shiftv = 2) {
    const int ptnum = static_cast<int>(x.size()) - 1;
    const double mean_x = std::accumulate(x.begin() + 1, x.end(), 0.0) / ptnum;
    const double mean_y = std::accumulate(y.begin() + 1, y.end(), 0.0) / ptnum;
    for (int i = 1; i <= ptnum; ++i) {
        x[i] -= mean_x;
        y[i] -= mean_y;
    }
    const double t0 = t[1];
    for (int i = 1; i <= ptnum; ++i) {
        t[i] -= t0;
    }

    const PrefixSums sums = build_prefix_sums(x, y, t);
    const std::vector<double> x_orig = x;
    const std::vector<double> y_orig = y;
    const std::vector<double> t_orig = t;

    std::vector<int> ptind(ptnum + 1, 0);
    for (int i = 1; i <= ptnum; ++i) {
        ptind[i] = i;
    }

    std::vector<double> thresholds;
    int test_m = ptnum / c;
    int cont = 2;
    while (test_m > 16 && cont > 0) {
        std::vector<int> path;
        if (test_m == 2) {
            path = {1, ptnum};
        } else {
            const double step = static_cast<double>(ptnum - 1) / static_cast<double>(test_m - 1);
            path.reserve(test_m);
            for (int k = 0; k < test_m; ++k) {
                int idx = static_cast<int>(std::llround(1.0 + k * step));
                idx = std::max(1, std::min(ptnum, idx));
                if (path.empty() || path.back() != idx) {
                    path.push_back(idx);
                }
            }
            if (path.back() != ptnum) {
                path.push_back(ptnum);
            }
        }

        const double dist = calc_issed_pathpt(x, y, t, path, sums);
        const double estval = dist / static_cast<double>(std::max(1, test_m - 1));
        thresholds.push_back(estval);
        test_m /= c;

        if (estval > lissed_threshold) {
            if (cont > 1) {
                cont = 1;
            } else if (cont == 1) {
                cont = 0;
            }
        }
    }

    std::sort(thresholds.begin(), thresholds.end());

    std::vector<std::vector<int>> path_ref;
    int scalenum = static_cast<int>(thresholds.size()) - 1;
    for (int i = 0; i < scalenum; ++i) {
        const double thre0 = thresholds[i];
        const double dthre = thresholds[i + 1];
        const InitResult init = pa_min_nsed_init(x, y, t, sums, thre0, dthre, ptind);
        const SimplifyResult simp = pa_mine_sed_crsdp(x, y, t, init.linkcell, sums, thre0, ptind);

        std::vector<int> mapped_path;
        mapped_path.reserve(simp.path.size());
        for (const int idx : simp.path) {
            mapped_path.push_back(ptind[idx]);
        }
        path_ref.push_back(mapped_path);

        std::vector<double> new_x(mapped_path.size() + 1, 0.0);
        std::vector<double> new_y(mapped_path.size() + 1, 0.0);
        std::vector<double> new_t(mapped_path.size() + 1, 0.0);
        std::vector<int> new_ptind(mapped_path.size() + 1, 0);
        for (std::size_t k = 0; k < mapped_path.size(); ++k) {
            new_ptind[k + 1] = mapped_path[k];
            new_x[k + 1] = x_orig[mapped_path[k]];
            new_y[k + 1] = y_orig[mapped_path[k]];
            new_t[k + 1] = t_orig[mapped_path[k]];
        }
        x = new_x;
        y = new_y;
        t = new_t;
        ptind = new_ptind;
    }

    x = x_orig;
    y = y_orig;
    t = t_orig;
    ptind.resize(ptnum + 1);
    for (int i = 1; i <= ptnum; ++i) {
        ptind[i] = i;
    }

    int sq0 = -1;
    for (std::size_t i = 0; i < thresholds.size(); ++i) {
        if (thresholds[i] >= lissed_threshold) {
            sq0 = static_cast<int>(i) + 1;
            break;
        }
    }

    int sq = sq0 - shiftv;
    std::vector<double> xs = x;
    std::vector<double> ys = y;
    std::vector<double> ts = t;
    std::vector<int> ptind_selected = ptind;

    if (sq > 0 && sq0 != -1) {
        const auto& selected_path = path_ref[sq - 1];
        xs.assign(selected_path.size() + 1, 0.0);
        ys.assign(selected_path.size() + 1, 0.0);
        ts.assign(selected_path.size() + 1, 0.0);
        ptind_selected.assign(selected_path.size() + 1, 0);
        for (std::size_t k = 0; k < selected_path.size(); ++k) {
            ptind_selected[k + 1] = selected_path[k];
            xs[k + 1] = x[selected_path[k]];
            ys[k + 1] = y[selected_path[k]];
            ts[k + 1] = t[selected_path[k]];
        }
    } else if (sq0 == -1 && !path_ref.empty()) {
        const auto& selected_path = path_ref.back();
        xs.assign(selected_path.size() + 1, 0.0);
        ys.assign(selected_path.size() + 1, 0.0);
        ts.assign(selected_path.size() + 1, 0.0);
        ptind_selected.assign(selected_path.size() + 1, 0);
        for (std::size_t k = 0; k < selected_path.size(); ++k) {
            ptind_selected[k + 1] = selected_path[k];
            xs[k + 1] = x[selected_path[k]];
            ys[k + 1] = y[selected_path[k]];
            ts[k + 1] = t[selected_path[k]];
        }
    }

    double dthre = std::numeric_limits<double>::infinity();
    if (sq0 != -1) {
        const int tar = sq0 + 1;
        if (tar <= static_cast<int>(thresholds.size())) {
            dthre = thresholds[tar - 1];
        }
    }

    const InitResult init = pa_min_nsed_init(xs, ys, ts, sums, lissed_threshold, dthre, ptind_selected);
    std::vector<int> refined_parent = init.parent;
    const auto refined_link = pa_min_msed_tree(xs, ys, ts, init.linkcell, refined_parent, sums, 2, lissed_threshold, ptind_selected);
    const SimplifyResult simp = pa_mine_sed_crsdp(xs, ys, ts, refined_link, sums, lissed_threshold, ptind_selected);

    std::vector<int> final_path;
    final_path.reserve(simp.path.size());
    for (const int idx : simp.path) {
        const int mapped = ptind_selected[idx];
        if (final_path.empty() || final_path.back() != mapped) {
            final_path.push_back(mapped);
        }
    }
    return final_path;
}

void write_output(
    const std::string& path,
    const std::vector<RawPoint>& raw,
    const std::vector<int>& selected_path,
    double runtime_seconds) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open output file: " + path);
    }

    output << std::fixed << std::setprecision(6);
    for (const int idx1 : selected_path) {
        const RawPoint& point = raw[idx1 - 1];
        output << point.lat << ' ' << point.lon << ' ' << point.unix_time << '\n';
    }
    output << runtime_seconds << '\n';
}

void write_preprocessed_output(
    const std::string& path,
    const std::vector<RawPoint>& raw,
    const ProjectedData& projected) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open preprocessed-output file: " + path);
    }

    output << std::fixed << std::setprecision(12);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        output << (i + 1) << ' ' << raw[i].lat << ' ' << raw[i].lon << ' '
               << raw[i].unix_time << ' ' << projected.x[i + 1] << ' '
               << projected.y[i + 1] << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 6) {
        std::cerr << "Usage: mrpa_cpp <input_geolife_plt> <lissed_threshold> <output_file> "
                     "[--dump-preprocessed <output_file>]\n";
        return 1;
    }

    try {
        const std::string input_file = argv[1];
        const double threshold = std::stod(argv[2]);
        const std::string output_file = argv[3];

        const std::vector<RawPoint> raw = read_geolife(input_file);
        const ProjectedData projected = mercator_project(raw);
        if (argc == 6) {
            if (std::string(argv[4]) != "--dump-preprocessed") {
                throw std::runtime_error("unknown option: " + std::string(argv[4]));
            }
            write_preprocessed_output(argv[5], raw, projected);
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<int> path = tdmrpa_sed(projected.x, projected.y, projected.t, threshold, 2, 2);
        const auto end = std::chrono::steady_clock::now();
        const double runtime_seconds = std::chrono::duration<double>(end - start).count();

        write_output(output_file, raw, path, runtime_seconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "MRPA C++ port failed: " << ex.what() << '\n';
        return 1;
    }
}
