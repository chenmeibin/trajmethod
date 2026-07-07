package io.github.xkitsios;

import java.util.ArrayList;
import java.util.List;

class DisjointPLA {
    private final boolean y;
    private final ConvexHull hull;
    private final List<Segment> segments;
    private final List<TimeSeriesPoint> segmentPoints;
    private Line lower;
    private Line upper;
    private boolean clearNext;

    protected DisjointPLA(boolean y) {
        this.y = y;
        this.hull = new ConvexHull();
        this.segments = new ArrayList<>();
        this.segmentPoints = new ArrayList<>();
        clearNext = false;
    }

    private void clear() {
        segmentPoints.clear();
        lower = null;
        upper = null;
        hull.clearHull();
    }

    protected boolean add(TimeSeriesPoint newPoint, double epsilon) {
        if (clearNext) clear();
        clearNext = false;
        segmentPoints.add(newPoint);
        if (segmentPoints.size() == 1) {
            hull.addToHull(newPoint, epsilon);
        } else if (segmentPoints.size() == 2) {
            lower = new Line(hull.get(0, true).getT(), hull.get(0, true).getV(), segmentPoints.get(1).getT(), segmentPoints.get(1).getV() - epsilon);
            upper = new Line(hull.get(0, false).getT(), hull.get(0, false).getV(), segmentPoints.get(1).getT(), segmentPoints.get(1).getV() + epsilon);
            hull.addToHull(newPoint, epsilon);
        } else {
            if (newPoint.getV() - epsilon > upper.valueAt(newPoint.getT()) || newPoint.getV() + epsilon < lower.valueAt(newPoint.getT())) {
                segments.add(new Segment(segmentPoints.getFirst().getT(), upper, lower, y));
                segmentPoints.removeLast();
                clearNext = true;
                return false;
            } else {
                hull.addToHull(newPoint, epsilon);
                if (lower.valueAt(newPoint.getT()) + epsilon < newPoint.getV()) {
                    for (int i = 0; i < hull.size(true) - 1; i++) {
                        Line tempLine = new Line(hull.get(i, true).getT(), hull.get(i, true).getV(), segmentPoints.getLast().getT(), segmentPoints.getLast().getV() - epsilon);
                        if (tempLine.getA() > lower.getA()) lower = tempLine;
                    }
                }
                if (upper.valueAt(newPoint.getT()) - epsilon > newPoint.getV()) {
                    for (int i = 0; i < hull.size(false) - 1; i++) {
                        Line tempLine = new Line(hull.get(i, false).getT(), hull.get(i, false).getV(), segmentPoints.getLast().getT(), segmentPoints.getLast().getV() + epsilon);
                        if (tempLine.getA() < upper.getA()) upper = tempLine;
                    }
                }
            }
        }

        return true;
    }

    protected void closeSegment() {
        if (segmentPoints.size() == 1)
            segments.add(new Segment(segmentPoints.getFirst().getT(), new Line(segmentPoints.getFirst().getT(), 0, hull.get(0, true).getV()), new Line(segmentPoints.getFirst().getT(), 0, hull.get(0, false).getV()), y));
        else if (segmentPoints.size() > 1) segments.add(new Segment(segmentPoints.getFirst().getT(), upper, lower, y));
        segmentPoints.clear();
        hull.clearHull();
    }

    protected List<Segment> getSegments() {
        return segments;
    }

    protected int segmentSize() {
        return segmentPoints.size();
    }
}
