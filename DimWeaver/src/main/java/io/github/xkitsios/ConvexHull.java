package io.github.xkitsios;

import java.util.ArrayList;
import java.util.List;

class ConvexHull {
    private final List<TimeSeriesPoint> upperHull;
    private final List<TimeSeriesPoint> lowerHull;

    protected ConvexHull() {
        upperHull = new ArrayList<>();
        lowerHull = new ArrayList<>();
    }

    protected void addToHull(TimeSeriesPoint p, double epsilon) {
        upperHull.add(new TimeSeriesPoint(p.getT(), p.getV() + epsilon));
        lowerHull.add(new TimeSeriesPoint(p.getT(), p.getV() - epsilon));
        updateHull(upperHull, true);
        updateHull(lowerHull, false);
    }

    private double crossProduct(TimeSeriesPoint o, TimeSeriesPoint a, TimeSeriesPoint b) {
        return (a.getT() - o.getT()) * (b.getV() - o.getV()) - (a.getV() - o.getV()) * (b.getT() - o.getT());
    }

    private void updateHull(List<TimeSeriesPoint> hull, boolean isUpper) {
        while (hull.size() > 2) {
            int size = hull.size();
            TimeSeriesPoint a = hull.get(size - 3);
            TimeSeriesPoint b = hull.get(size - 2);
            TimeSeriesPoint c = hull.get(size - 1);

            if ((isUpper && crossProduct(a, b, c) < 0) || (!isUpper && crossProduct(a, b, c) > 0)) {
                hull.remove(size - 2);
            } else {
                break;
            }
        }
    }

    protected int size(boolean upper) {
        if (upper) return upperHull.size();
        else return lowerHull.size();
    }

    protected TimeSeriesPoint get(int index, boolean upper) {
        if (upper) return upperHull.get(index);
        else return lowerHull.get(index);
    }

    protected void clearHull() {
        upperHull.clear();
        lowerHull.clear();
    }
}
