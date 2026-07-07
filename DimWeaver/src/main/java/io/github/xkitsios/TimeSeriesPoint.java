package io.github.xkitsios;

class TimeSeriesPoint {
    private final long t;
    private final double v;

    protected TimeSeriesPoint(long t, double v) {
        this.t = t;
        this.v = v;
    }

    protected long getT() {
        return t;
    }

    protected double getV() {
        return v;
    }
}
