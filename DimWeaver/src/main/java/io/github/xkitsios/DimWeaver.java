package io.github.xkitsios;

import java.io.IOException;
import java.util.*;

public class DimWeaver {
    public static byte[] compress(List<TrajectoryPoint> points, double epsilon, double c) throws IOException {
        return Binary.encode(KinematicGrouping.group(dimWeaver(points, epsilon, c)), points.getFirst().t());
    }

    public static List<List<Line>> decompress(byte[] binary) throws IOException {
        return Binary.decode(binary);
    }

    public static List<Segment> dimWeaver(List<TrajectoryPoint> points, double epsilon, double c) {
        DisjointPLA disjointPLAX = new DisjointPLA(false), disjointPLAY = new DisjointPLA(true);
        List<TimeSeriesPoint> bufferX = new ArrayList<>();
        List<TimeSeriesPoint> bufferY = new ArrayList<>();
        double exponentialDecayX = 0, exponentialDecayY = 0;
        boolean residualY = false; // true: residual@Y, false: residual@X
        boolean added;
        for (TrajectoryPoint point : points) {
            TimeSeriesPoint pointX = new TimeSeriesPoint(point.t(), point.x()), pointY = new TimeSeriesPoint(point.t(), point.y());
            bufferX.add(pointX);
            bufferY.add(pointY);
            added = (residualY) ? disjointPLAX.add(pointX, epsilon / Math.sqrt(2)) : disjointPLAY.add(pointY, epsilon / Math.sqrt(2));
            if (!added) {
                if (residualY) exponentialDecayX = exponentialDecayX * (1 - c) + disjointPLAX.segmentSize() * c;
                else exponentialDecayY = exponentialDecayY * (1 - c) + disjointPLAY.segmentSize() * c;
                Segment lastSegment = (residualY) ? disjointPLAX.getSegments().getLast() : disjointPLAY.getSegments().getLast();
                List<TimeSeriesPoint> bufferUncompressed = (residualY) ? bufferY : bufferX;
                List<TimeSeriesPoint> bufferCompressed = (residualY) ? bufferX : bufferY;
                for (int i = 0; i < bufferUncompressed.size() - 1; i++) {
                    added = (residualY) ? disjointPLAY.add(bufferUncompressed.get(i), calculateResidual(lastSegment, bufferCompressed.get(i), epsilon)) : disjointPLAX.add(bufferUncompressed.get(i), calculateResidual(lastSegment, bufferCompressed.get(i), epsilon));
                    if (!added) {
                        if (residualY) exponentialDecayY = exponentialDecayY * (1 - c) + disjointPLAY.segmentSize() * c;
                        else exponentialDecayX = exponentialDecayX * (1 - c) + disjointPLAX.segmentSize() * c;

                        if (residualY)
                            disjointPLAY.add(bufferUncompressed.get(i), calculateResidual(lastSegment, bufferCompressed.get(i), epsilon));
                        else
                            disjointPLAX.add(bufferUncompressed.get(i), calculateResidual(lastSegment, bufferCompressed.get(i), epsilon));
                    }
                }
                residualY = exponentialDecayX * c > exponentialDecayY * c;
                added = (residualY) ? disjointPLAX.add(pointX, epsilon / Math.sqrt(2)) : disjointPLAY.add(pointY, epsilon / Math.sqrt(2));
                if (!added) {
                    if (residualY) exponentialDecayX = exponentialDecayX * (1 - c) + disjointPLAX.segmentSize() * c;
                    else exponentialDecayY = exponentialDecayY * (1 - c) + disjointPLAY.segmentSize() * c;
                    residualY = exponentialDecayX * c > exponentialDecayY * c;
                    if (residualY) disjointPLAX.add(pointX, epsilon / Math.sqrt(2));
                    else disjointPLAY.add(pointY, epsilon / Math.sqrt(2));
                }

                bufferX.clear();
                bufferX.add(pointX);
                bufferY.clear();
                bufferY.add(pointY);
            }
        }

        if (residualY) {
            disjointPLAX.closeSegment();
            Segment lastSegment = disjointPLAX.getSegments().getLast();
            for (int i = 0; i < bufferY.size(); i++)
                if (!disjointPLAY.add(bufferY.get(i), calculateResidual(lastSegment, bufferX.get(i), epsilon)))
                    disjointPLAY.add(bufferY.get(i), calculateResidual(lastSegment, bufferX.get(i), epsilon));
            disjointPLAY.closeSegment();
        } else {
            disjointPLAY.closeSegment();
            Segment lastSegment = disjointPLAY.getSegments().getLast();
            for (int i = 0; i < bufferX.size(); i++)
                if (!disjointPLAX.add(bufferX.get(i), calculateResidual(lastSegment, bufferY.get(i), epsilon)))
                    disjointPLAX.add(bufferX.get(i), calculateResidual(lastSegment, bufferY.get(i), epsilon));
            disjointPLAX.closeSegment();
        }

        disjointPLAX.getSegments().addAll(disjointPLAY.getSegments());

        return disjointPLAX.getSegments();
    }

    private static double calculateResidual(Segment segment, TimeSeriesPoint point, double epsilon) {
        double maxError = Math.max(Math.abs(segment.getUpper().valueAt(point.getT()) - point.getV()), Math.abs(segment.getLower().valueAt(point.getT()) - point.getV()));

        return Math.max(Math.sqrt(epsilon * epsilon - maxError * maxError), epsilon / Math.sqrt(2));
    }

    public static List<TrajectoryPoint> reconstructTrajectory(List<List<Line>> lines, List<TrajectoryPoint> original) {
        TrajectoryPoint[] reconstructed = new TrajectoryPoint[original.size()];
        int reconstructedIdx = 0;

        int xIdx = 0, yIdx = 0, numOfSegmentsX = lines.get(0).size(), numOfSegmentsY = lines.get(1).size();
        long nextTimestampX = (long) lines.get(0).get(Math.min(xIdx + 1, lines.get(0).size() - 1)).getTStart(), nextTimestampY = (long) lines.get(1).get(Math.min(yIdx + 1, lines.get(1).size() - 1)).getTStart();
        Line middleX = lines.get(0).get(xIdx), middleY = lines.get(1).get(yIdx);
        for (TrajectoryPoint trajectoryPoint : original) {
            long t = trajectoryPoint.t();
            if (xIdx + 1 < numOfSegmentsX && nextTimestampX <= t) {
                xIdx++;
                nextTimestampX = (long) lines.get(0).get(Math.min(xIdx + 1, lines.get(0).size() - 1)).getTStart();
                middleX = lines.get(0).get(xIdx);
            }
            if (yIdx + 1 < numOfSegmentsY && nextTimestampY <= t) {
                yIdx++;
                nextTimestampY = (long) lines.get(1).get(Math.min(yIdx + 1, lines.get(1).size() - 1)).getTStart();
                middleY = lines.get(1).get(yIdx);
            }
            double x = middleX.valueAt(t);
            double y = middleY.valueAt(t);
            reconstructed[reconstructedIdx++] = new TrajectoryPoint(t, x, y);
        }

        return Arrays.asList(reconstructed);
    }
}