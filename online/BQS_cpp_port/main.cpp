#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTolerance = 1e-3;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

double pointToPointDistance(const Point& a, const Point& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

double pointToLineSegmentDistance(
    double xA,
    double yA,
    double xB,
    double yB,
    double xC,
    double yC,
    bool infiniteLine = false) {
    const double abx = xB - xA;
    const double aby = yB - yA;
    const double acx = xC - xA;
    const double acy = yC - yA;
    const double bcx = xC - xB;
    const double bcy = yC - yB;

    const double ab = std::sqrt(abx * abx + aby * aby);
    const double ac = std::sqrt(acx * acx + acy * acy);
    const double bc = std::sqrt(bcx * bcx + bcy * bcy);

    if (!infiniteLine) {
        if (ab * ab + ac * ac <= bc * bc) {
            return ac;
        }
        if (ab * ab + bc * bc <= ac * ac) {
            return bc;
        }
    }

    if (std::abs(ab) < 1e-9) {
        return ac;
    }

    const double cross = std::abs(abx * acy - aby * acx);
    return cross / ab;
}

double pointToLineDistance(const Point& s, const Point& e, const Point& p) {
    return pointToLineSegmentDistance(s.x, s.y, e.x, e.y, p.x, p.y, false);
}

double getAngle(const Point& p0, const Point& p1, bool toPositive = true) {
    double angle = std::atan2(p1.y - p0.y, p1.x - p0.x);
    if (toPositive && angle < 0.0) {
        angle = 2.0 * kPi + angle;
    }
    return angle;
}

Point rotatePoint(const Point& x, double theta, const Point& pivot) {
    const double dx = x.x - pivot.x;
    const double dy = x.y - pivot.y;
    return Point{
        dx * std::cos(theta) - dy * std::sin(theta) + pivot.x,
        dx * std::sin(theta) + dy * std::cos(theta) + pivot.y};
}

Point getProjection(const Point& s, const Point& e, const Point& x) {
    const double r = pointToPointDistance(s, x);
    const double theta0 = getAngle(s, e);
    const double theta1 = getAngle(s, x);
    const double theta = theta0 - theta1;
    const double projR = r * std::cos(theta);
    return Point{
        projR * std::cos(theta) + s.x,
        projR * std::sin(theta) + s.y};
}

void insertUniquePoint(std::vector<Point>& points, const Point& p, double tolerance) {
    for (const auto& existing : points) {
        if (std::abs(existing.x - p.x) < tolerance && std::abs(existing.y - p.y) < tolerance) {
            return;
        }
    }
    points.push_back(p);
}

class BoundingLines {
public:
    explicit BoundingLines(Point start) : start_(start) {}

    void addPoint(const Point& p, double angle = std::numeric_limits<double>::quiet_NaN()) {
        newPoint_ = p;
        newAngle_ = std::isnan(angle) ? getAngle(start_, p) : angle;
        update();
    }

    double getB(double angle) const {
        return start_.y - std::tan(angle) * start_.x;
    }

    bool hasLowerAngle() const { return hasLowerAngle_; }
    bool hasUpperAngle() const { return hasUpperAngle_; }
    double lowerAngle() const { return lowerAngle_; }
    double upperAngle() const { return upperAngle_; }
    const Point& lowerPoint() const { return lowerPoint_; }
    const Point& upperPoint() const { return upperPoint_; }
    double lowerAngleB() const { return lowerAngleB_; }
    double upperAngleB() const { return upperAngleB_; }

private:
    void update() {
        if (!hasUpperAngle_ || newAngle_ > upperAngle_) {
            upperAngle_ = newAngle_;
            upperPoint_ = newPoint_;
            upperAngleB_ = getB(newAngle_);
            hasUpperAngle_ = true;
        }

        if (!hasLowerAngle_ || newAngle_ < lowerAngle_) {
            lowerAngle_ = newAngle_;
            lowerPoint_ = newPoint_;
            lowerAngleB_ = getB(newAngle_);
            hasLowerAngle_ = true;
        }
    }

    Point start_;
    bool hasLowerAngle_ = false;
    bool hasUpperAngle_ = false;
    double lowerAngle_ = 0.0;
    double upperAngle_ = 0.0;
    Point lowerPoint_{};
    Point upperPoint_{};
    double lowerAngleB_ = 0.0;
    double upperAngleB_ = 0.0;
    Point newPoint_{};
    double newAngle_ = 0.0;
};

class BoundingBox {
public:
    explicit BoundingBox(Point start) : start_(start) {}

    void addPoint(const Point& p) {
        if (!initialized_) {
            minX_ = maxX_ = p.x;
            minY_ = maxY_ = p.y;
            initialized_ = true;
            return;
        }
        minX_ = std::min(minX_, p.x);
        maxX_ = std::max(maxX_, p.x);
        minY_ = std::min(minY_, p.y);
        maxY_ = std::max(maxY_, p.y);
    }

    std::array<double, 4> getBox() const { return {minX_, maxX_, minY_, maxY_}; }

    std::vector<Point> getCorners() const {
        return {
            Point{minX_, minY_},
            Point{maxX_, minY_},
            Point{maxX_, maxY_},
            Point{minX_, maxY_}};
    }

    bool pointInBox(const Point& p) const {
        return p.x <= maxX_ + kTolerance && p.x >= minX_ - kTolerance &&
               p.y <= maxY_ + kTolerance && p.y >= minY_ - kTolerance;
    }

    std::pair<std::vector<Point>, std::vector<Point>> getIntersection(const BoundingLines& lines) const {
        return {intersectLine(lines.lowerAngle(), lines.lowerAngleB()), intersectLine(lines.upperAngle(), lines.upperAngleB())};
    }

    std::vector<Point> intersectLine(double angle, double b, bool strict = false) const {
        std::vector<Point> result;
        for (double x : {minX_, maxX_}) {
            auto p = intersectVertical(angle, b, x, minY_, maxY_);
            if (p.hasValue) {
                if (!strict || pointInBox(p.value)) {
                    insertUniquePoint(result, p.value, kTolerance);
                }
            }
        }
        for (double y : {minY_, maxY_}) {
            auto p = intersectHorizontal(angle, b, y, minX_, maxX_);
            if (p.hasValue) {
                if (!strict || pointInBox(p.value)) {
                    insertUniquePoint(result, p.value, kTolerance);
                }
            }
        }
        return result;
    }

    double minX() const { return minX_; }
    double minY() const { return minY_; }

private:
    struct OptionalPoint {
        bool hasValue = false;
        Point value{};
    };

    static OptionalPoint intersectVertical(double angle, double b, double x, double minY, double maxY) {
        const double y = std::tan(angle) * x + b;
        if (y <= maxY + kTolerance && y >= minY - kTolerance) {
            return {true, Point{x, y}};
        }
        return {};
    }

    static OptionalPoint intersectHorizontal(double angle, double b, double y, double minX, double maxX) {
        double x = maxX;
        if (std::abs(angle) > 1e-12) {
            x = (y - b) / std::tan(angle);
        }
        if (x <= maxX + kTolerance && x >= minX - kTolerance) {
            return {true, Point{x, y}};
        }
        return {};
    }

    Point start_;
    bool initialized_ = false;
    double minX_ = 0.0;
    double maxX_ = 0.0;
    double minY_ = 0.0;
    double maxY_ = 0.0;
};

class TrajProcessor;

class QuadrantSystem {
public:
    QuadrantSystem(Point start, TrajProcessor* master, int quadrantIdx)
        : start_(start), box_(start), lines_(start), master_(master), quadrantIdx_(quadrantIdx) {}

    void processPoint(const Point& p);
    void addPoint(const Point& p);
    std::pair<double, double> getBounds(const Point& p);

private:
    std::pair<bool, bool> angleInQuadrant(double angle) const;

    Point start_;
    bool hasCentroid_ = false;
    Point centroid_{};
    BoundingBox box_;
    BoundingLines lines_;
    std::vector<Point> intersections_;
    bool hasAngleRange_ = false;
    std::array<double, 2> angleRange_{};

    double newAngle_ = 0.0;
    int pointCount_ = 0;

    std::pair<double, double> bounds_{0.0, 0.0};
    TrajProcessor* master_;
    int quadrantIdx_;

    bool hasFarthestPoint_ = false;
    Point farthestPoint_{};
    Point lastProj_{};
    double maxLen_ = 0.0;
    double curDist_ = 0.0;
    bool curDistPos_ = false;
    bool hasCurMaxDist_ = false;
    double curMaxDist_ = 0.0;
    Point proj_{};
};

class TrajProcessor {
public:
    TrajProcessor(Point start, double eps, double prevErr = 0.0) {
        reInit(start, eps, prevErr);
    }

    void reInit(Point start, double eps, double prevErr = 0.0) {
        quadrants_.fill(nullptr);
        ownedQuadrants_.clear();
        start_ = start;
        end_ = {};
        points_.clear();
        lowerBound_ = 0.0;
        upperBound_ = 0.0;
        proCount_ = 0;
        epsilon_ = eps;
        hasLastComputed_ = false;
        lastComputed_ = 0.0;
        lastComputedPoint_ = {};
        maxD_ = 0.0;
        lastMaxPoint_ = {};
        lastAngle_ = 0.0;
        lastPos_ = false;
        currMax_ = 0.0;
        small_ = {0.0, 0.0, 0.0, 0.0};
        prevErr_ = prevErr;
    }

    int getQuadrantIdx(const Point& p) const {
        const double a = p.x - start_.x;
        const double b = p.y - start_.y;
        if (a > 0.0 && b >= 0.0) {
            return 0;
        }
        if (a <= 0.0 && b > 0.0) {
            return 1;
        }
        if (a < 0.0 && b <= 0.0) {
            return 2;
        }
        if (a >= 0.0 && b < 0.0) {
            return 3;
        }
        return 0;
    }

    int processPoint(const Point& p) {
        const int idx = getQuadrantIdx(p);
        end_ = p;
        std::vector<double> lbs;
        std::vector<double> ubs;

        if (!points_.empty()) {
            if (hasLastComputed_) {
                const double d = pointToLineDistance(start_, lastComputedPoint_, p);
                if (d > currMax_) {
                    currMax_ = d;
                }
            }

            if (points_.size() <= 4) {
                const double maxd = getFarthest(false);
                lbs.push_back(maxd);
                ubs.push_back(maxd);
            } else {
                for (auto* q : quadrants_) {
                    if (q != nullptr) {
                        q->processPoint(p);
                        const auto bounds = q->getBounds(p);
                        lbs.push_back(bounds.first);
                        ubs.push_back(bounds.second);
                    }
                }
            }

            if (!lbs.empty()) {
                lowerBound_ = *std::max_element(lbs.begin(), lbs.end());
                upperBound_ = *std::max_element(ubs.begin(), ubs.end());
            }
        }
        return idx;
    }

    void postProcess(const Point& p, int idx) {
        const double d = pointToPointDistance(start_, end_);
        if (d <= epsilon_ - prevErr_) {
            if (small_[idx] < d) {
                small_[idx] = d;
            }
            return;
        }

        if (quadrants_[idx] == nullptr) {
            ownedQuadrants_.push_back(std::make_unique<QuadrantSystem>(start_, this, idx));
            quadrants_[idx] = ownedQuadrants_.back().get();
        }

        quadrants_[idx]->addPoint(p);
        points_.push_back(p);
    }

    double getFarthest(bool count) {
        double maxD = 0.0;
        if (!points_.empty()) {
            std::size_t maxI = 0;
            for (std::size_t i = 0; i < points_.size(); ++i) {
                const double d = pointToLineDistance(start_, end_, points_[i]);
                if (d > maxD) {
                    maxI = i;
                    maxD = d;
                }
            }

            if (count) {
                ++proCount_;
                hasLastComputed_ = true;
                lastComputed_ = maxD;
                lastComputedPoint_ = end_;
                lastAngle_ = getAngle(start_, end_);
                lastPos_ = getAngle(start_, points_[maxI]) < lastAngle_;
                for (auto* q : quadrants_) {
                    if (q != nullptr) {
                        q->processPoint(end_);
                    }
                }
                lastMaxPoint_ = points_[maxI];
                currMax_ = maxD;
            }
        }
        maxD_ = maxD;
        return maxD;
    }

    std::pair<int, double> decision(bool conservative = false) {
        double maxD = 0.0;

        if (lowerBound_ > epsilon_) {
            return {-1, lowerBound_};
        }

        if (upperBound_ + prevErr_ <= epsilon_) {
            return {0, 0.0};
        }

        if (!conservative) {
            if (points_.size() <= 4) {
                maxD = upperBound_;
            } else {
                maxD = getFarthest(true);
            }

            if (maxD + prevErr_ > epsilon_) {
                return {-1, maxD};
            }
            return {0, maxD};
        }

        return {-1, maxD};
    }

    const Point& start() const { return start_; }
    const Point& end() const { return end_; }
    const std::vector<Point>& points() const { return points_; }
    double epsilon() const { return epsilon_; }
    double prevErr() const { return prevErr_; }
    double lowerBound() const { return lowerBound_; }
    double upperBound() const { return upperBound_; }
    double maxD() const { return maxD_; }
    bool hasLastComputed() const { return hasLastComputed_; }
    const Point& lastComputedPoint() const { return lastComputedPoint_; }
    double lastAngle() const { return lastAngle_; }
    bool lastPos() const { return lastPos_; }
    double currMax() const { return currMax_; }
    std::array<double, 4>& small() { return small_; }

private:
    std::array<QuadrantSystem*, 4> quadrants_{};
    std::vector<std::unique_ptr<QuadrantSystem>> ownedQuadrants_;
    Point start_{};
    Point end_{};
    std::vector<Point> points_;
    double lowerBound_ = 0.0;
    double upperBound_ = 0.0;
    int proCount_ = 0;
    double epsilon_ = 0.0;
    bool hasLastComputed_ = false;
    double lastComputed_ = 0.0;
    Point lastComputedPoint_{};
    double maxD_ = 0.0;
    Point lastMaxPoint_{};
    double lastAngle_ = 0.0;
    bool lastPos_ = false;
    double currMax_ = 0.0;
    std::array<double, 4> small_{};
    double prevErr_ = 0.0;
};

void QuadrantSystem::processPoint(const Point& p) {
    if (!hasFarthestPoint_) {
        farthestPoint_ = p;
        maxLen_ = pointToPointDistance(start_, p);
        lastProj_ = p;
        hasFarthestPoint_ = true;
        return;
    }

    if (pointToPointDistance(start_, p) > maxLen_) {
        farthestPoint_ = p;
        maxLen_ = pointToPointDistance(start_, p);
    }

    if (master_->hasLastComputed()) {
        lastProj_ = getProjection(start_, master_->lastComputedPoint(), farthestPoint_);
        curDist_ = pointToLineDistance(start_, master_->lastComputedPoint(), p);
        curDistPos_ = newAngle_ < master_->lastAngle();

        const double theta = getAngle(start_, p);
        const Point pp{
            maxLen_ * std::cos(theta) + start_.x,
            maxLen_ * std::sin(theta) + start_.y};
        proj_ = pp;

        if (!hasCurMaxDist_) {
            curMaxDist_ = master_->currMax();
            hasCurMaxDist_ = true;
        }

        const double d = pointToLineDistance(start_, master_->lastComputedPoint(), pp);
        if (d > curMaxDist_) {
            curMaxDist_ = d;
        }
    }
}

void QuadrantSystem::addPoint(const Point& p) {
    newAngle_ = getAngle(start_, p);

    if (!hasAngleRange_) {
        const double lower = std::floor(newAngle_ / (kPi / 2.0)) * (kPi / 2.0);
        angleRange_ = {lower, lower + (kPi / 2.0)};
        hasAngleRange_ = true;
    }

    box_.addPoint(p);
    lines_.addPoint(p);

    if (!hasCentroid_) {
        centroid_ = p;
        hasCentroid_ = true;
    } else {
        centroid_.x = (centroid_.x * pointCount_ + p.x) / static_cast<double>(pointCount_ + 1);
        centroid_.y = (centroid_.y * pointCount_ + p.y) / static_cast<double>(pointCount_ + 1);
    }

    ++pointCount_;
}

std::pair<bool, bool> QuadrantSystem::angleInQuadrant(double angle) const {
    const bool inQuadrant = angle >= angleRange_[0] && angle < angleRange_[1];
    const bool inLines =
        lines_.hasLowerAngle() && lines_.hasUpperAngle() &&
        angle >= lines_.lowerAngle() && angle <= lines_.upperAngle();
    return {inQuadrant, inLines};
}

std::pair<double, double> QuadrantSystem::getBounds(const Point& p) {
    const auto linePoints = box_.getIntersection(lines_);

    if (pointCount_ == 1) {
        const double d = pointToLineSegmentDistance(
            start_.x, start_.y, p.x, p.y, box_.minX(), box_.minY());
        bounds_ = {d, d};
        return bounds_;
    }

    std::vector<double> lbDists;
    for (const auto& point : linePoints.first) {
        lbDists.push_back(pointToLineDistance(start_, p, point));
    }
    const double lbpDist = pointToLineDistance(start_, p, lines_.lowerPoint());

    std::vector<double> ubDists;
    for (const auto& point : linePoints.second) {
        ubDists.push_back(pointToLineDistance(start_, p, point));
    }
    const double ubpDist = pointToLineDistance(start_, p, lines_.upperPoint());

    const auto corners = box_.getCorners();
    std::vector<double> cornerDists;
    std::vector<double> farCornerValues;
    for (const auto& corner : corners) {
        cornerDists.push_back(pointToLineDistance(start_, p, corner));
        const double pNorm = std::sqrt(p.x * p.x + p.y * p.y);
        farCornerValues.push_back(pNorm > 0.0 ? (corner.x * p.x + corner.y * p.y) / pNorm : 0.0);
    }
    const double farCorner = *std::max_element(farCornerValues.begin(), farCornerValues.end());

    const double angle = getAngle(start_, p);
    const auto [inQuadrant, inLines] = angleInQuadrant(angle);
    const bool inBox = !box_.intersectLine(angle, lines_.getB(angle)).empty();

    if (inQuadrant) {
        std::vector<double> cDists;
        if (quadrantIdx_ % 2 == 0) {
            cDists = {cornerDists[0], cornerDists[2]};
        } else {
            cDists = {cornerDists[1], cornerDists[3]};
        }

        const double pNorm = std::sqrt(p.x * p.x + p.y * p.y);
        const double cDist = pNorm > farCorner ? *std::max_element(cDists.begin(), cDists.end())
                                               : *std::min_element(cDists.begin(), cDists.end());

        std::vector<double> merged = lbDists;
        merged.insert(merged.end(), ubDists.begin(), ubDists.end());
        merged.insert(merged.end(), cDists.begin(), cDists.end());

        if (inLines || inBox) {
            bounds_ = {
                std::max({*std::min_element(lbDists.begin(), lbDists.end()),
                          *std::min_element(ubDists.begin(), ubDists.end()),
                          lbpDist,
                          ubpDist,
                          cDist}),
                *std::max_element(merged.begin(), merged.end())};
        } else {
            bounds_ = {
                std::max({*std::min_element(lbDists.begin(), lbDists.end()),
                          *std::min_element(ubDists.begin(), ubDists.end()),
                          lbpDist,
                          ubpDist,
                          cDist}),
                *std::max_element(merged.begin(), merged.end())};
        }
    } else {
        auto sortedCorner = cornerDists;
        std::sort(sortedCorner.begin(), sortedCorner.end());
        bounds_ = {
            std::max({*std::min_element(lbDists.begin(), lbDists.end()),
                      *std::min_element(ubDists.begin(), ubDists.end()),
                      sortedCorner[2],
                      lbpDist,
                      ubpDist}),
            *std::max_element(cornerDists.begin(), cornerDists.end())};
    }

    return bounds_;
}

double bestAngle(const std::vector<Point>& points, const TrajProcessor& obj, std::size_t i) {
    std::array<std::vector<Point>, 4> pointBuckets;
    std::array<int, 4> pointCounts{0, 0, 0, 0};
    const std::array<double, 4> offsets{kPi * 0.5, kPi * 1.0, kPi * 1.5, kPi * 2.0};

    constexpr std::size_t sampleSize = 10;
    for (std::size_t j = 1; j < sampleSize; ++j) {
        if (i + j < points.size()) {
            const int idx = obj.getQuadrantIdx(points[i + j]);
            pointBuckets[idx].push_back(points[i + j]);
            ++pointCounts[idx];
        }
    }

    int maxJ = 0;
    for (int j = 1; j < 4; ++j) {
        if (pointCounts[maxJ] < pointCounts[j]) {
            maxJ = j;
        }
    }

    if (pointCounts[maxJ] == 0) {
        return 0.0;
    }

    Point mean{};
    for (const auto& point : pointBuckets[maxJ]) {
        mean.x += point.x;
        mean.y += point.y;
    }
    mean.x /= static_cast<double>(pointBuckets[maxJ].size());
    mean.y /= static_cast<double>(pointBuckets[maxJ].size());

    const double targetAngle = getAngle(obj.start(), mean);
    return offsets[maxJ] - targetAngle;
}

std::vector<int> runBqs(const std::vector<Point>& points, double epsilon) {
    if (points.empty()) {
        return {};
    }

    TrajProcessor* obj = nullptr;
    TrajProcessor processor(points[0], epsilon);
    std::vector<int> kept{0};
    std::size_t i = 0;
    double angle = 0.0;

    while (i < points.size()) {
        if (obj == nullptr) {
            processor.reInit(points[i], epsilon);
            obj = &processor;
            angle = bestAngle(points, *obj, i);
            ++i;
            continue;
        }

        Point candidate = points[i];
        if (angle != 0.0) {
            candidate = rotatePoint(candidate, angle, obj->start());
        }

        const int idx = obj->processPoint(candidate);
        const auto [decision, maxD] = obj->decision(false);
        (void)maxD;

        if (decision == -1) {
            kept.push_back(static_cast<int>(i - 1));
            processor.reInit(points[i - 1], epsilon);
            angle = bestAngle(points, processor, i - 1);
            obj = &processor;
        } else {
            obj->postProcess(obj->end(), idx);
            ++i;
        }
    }

    if (kept.back() != static_cast<int>(points.size() - 1)) {
        kept.push_back(static_cast<int>(points.size() - 1));
    }
    return kept;
}

struct InputData {
    std::vector<Point> points;
    std::vector<std::string> rawLines;
};

InputData readInput(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open input file: " + path);
    }

    InputData data;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        Point p{};
        if (!(iss >> p.x >> p.y)) {
            continue;
        }
        data.points.push_back(p);
        data.rawLines.push_back(line);
    }
    return data;
}

void writeOutput(const std::string& path, const std::vector<std::string>& rawLines, const std::vector<int>& kept, double runtimeSeconds) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open output file: " + path);
    }

    for (int index : kept) {
        output << rawLines[static_cast<std::size_t>(index)] << '\n';
    }
    output << std::fixed << std::setprecision(6) << runtimeSeconds << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: BQS_cpp.exe <input_path> <epsilon> <output_path>\n";
            return 1;
        }

        const std::string inputPath = argv[1];
        const double epsilon = std::stod(argv[2]);
        const std::string outputPath = argv[3];

        const InputData input = readInput(inputPath);
        if (input.points.empty()) {
            std::ofstream output(outputPath);
            output << std::fixed << std::setprecision(6) << 0.0 << '\n';
            return 0;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<int> kept = runBqs(input.points, epsilon);
        const auto end = std::chrono::steady_clock::now();
        const double runtimeSeconds = std::chrono::duration<double>(end - start).count();

        writeOutput(outputPath, input.rawLines, kept, runtimeSeconds);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
