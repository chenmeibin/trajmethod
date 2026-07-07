package io.github.xkitsios;

public class Line {
    private final double tStart;
    private final double a;
    private final double b;

    public Line(double t1, double v1, double t2, double v2) {
        this.tStart = t1;
        this.a = (v2 - v1) / (t2 - t1);
        this.b = v1;
    }

    public Line(double tStart, double a, double b) {
        this.tStart = tStart;
        this.a = a;
        this.b = b;
    }

    public static Line getMiddleLine(double tStart, Line upper, Line lower) {
        if (upper.getA() == lower.getA()) return new Line(tStart, upper.getA(), (lower.getB() + upper.getB()) / 2);
        double intersectionT = (upper.getA() * upper.getTStart() - upper.getB() - lower.getA() * lower.getTStart() + lower.getB()) / (upper.getA() - lower.getA());
        double intersectionV = lower.getA() * (intersectionT - lower.getTStart()) + lower.getB();
        double aMiddle = Math.tan((Math.atan(lower.getA()) + Math.atan(upper.getA())) / 2);

        return new Line(tStart, aMiddle, intersectionV - aMiddle * (intersectionT - tStart));
    }

    public double valueAt(double t) {
        return this.a * (t - this.tStart) + this.b;
    }

    public double getTStart() {
        return tStart;
    }

    public double getA() {
        return a;
    }

    public double getB() {
        return b;
    }
}
