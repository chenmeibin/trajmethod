package io.github.xkitsios;

public class Segment {
    private final long start;
    private final boolean dim;
    private Line upper;
    private Line middle;
    private Line lower;

    public Segment(long start, Line upper, Line lower, boolean dim) {
        this.start = start;
        if (upper.getTStart() != start) this.upper = new Line(start, upper.getA(), upper.valueAt(start));
        else this.upper = upper;
        this.middle = null;
        if (lower.getTStart() != start) this.lower = new Line(start, lower.getA(), lower.valueAt(start));
        else this.lower = lower;
        this.dim = dim;
    }

    public long getStart() {
        return start;
    }

    public Line getUpper() {
        return upper;
    }

    public Line getMiddle() {
        if (middle == null) middle = Line.getMiddleLine(start, upper, lower);
        return middle;
    }

    public Line getLower() {
        return lower;
    }

    public boolean getDim() {
        return dim;
    }

    public double getSmallestSlope() {
        return Math.min(lower.getA(), upper.getA());
    }

    public double getBiggestSlope() {
        return Math.max(lower.getA(), upper.getA());
    }

    public void updateSlopes(double smallest, double biggest) {
        double aLow, aUp;
        if (lower.getA() < upper.getA()) {
            aLow = smallest;
            aUp = biggest;
        } else if (lower.getA() > upper.getA()) {
            aLow = biggest;
            aUp = smallest;
        } else {
            return;
        }
        double intersectionT = (upper.getA() * upper.getTStart() - upper.getB() - lower.getA() * lower.getTStart() + lower.getB()) / (upper.getA() - lower.getA());
        double intersectionV = lower.getA() * (intersectionT - lower.getTStart()) + lower.getB();
        lower = new Line(lower.getTStart(), aLow, intersectionV - aLow * (intersectionT - lower.getTStart()));
        upper = new Line(upper.getTStart(), aUp, intersectionV - aUp * (intersectionT - upper.getTStart()));
        middle = null;
    }
}

