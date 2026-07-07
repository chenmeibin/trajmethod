package io.github.xkitsios;

import java.util.*;

class KinematicGrouping {
    public static Map<Double, List<Segment>> group(List<Segment> segments) {
        Map<Double, List<Segment>> groupedSegments = new HashMap<>();
        double aLowerGroup = -Double.MAX_VALUE;
        double aUpperGroup = Double.MAX_VALUE;
        ArrayList<Segment> group = new ArrayList<>();

        segments.sort(Comparator.comparingDouble(Segment::getSmallestSlope));

        for (Segment plaSegment : segments) {
            if (plaSegment.getSmallestSlope() == 0 && plaSegment.getBiggestSlope() == 0) {
                List<Segment> lastPoint = groupedSegments.getOrDefault(0.0, new ArrayList<>());
                lastPoint.add(plaSegment);
                groupedSegments.put(0.0, lastPoint);
                continue;
            }
            if (plaSegment.getSmallestSlope() <= aUpperGroup && plaSegment.getBiggestSlope() >= aLowerGroup) {
                aLowerGroup = Math.max(aLowerGroup, plaSegment.getSmallestSlope());
                aUpperGroup = Math.min(aUpperGroup, plaSegment.getBiggestSlope());
                group.add(plaSegment);
            } else {
                for (Segment groupedSegment : group)
                    groupedSegment.updateSlopes(aLowerGroup, aUpperGroup);
                if (groupedSegments.containsKey(group.getFirst().getMiddle().getA()))
                    group.addAll(groupedSegments.get(group.getFirst().getMiddle().getA()));
                groupedSegments.put(group.getFirst().getMiddle().getA(), group);
                group = new ArrayList<>();
                group.add(plaSegment);
                aLowerGroup = plaSegment.getSmallestSlope();
                aUpperGroup = plaSegment.getBiggestSlope();
            }
        }
        for (Segment groupedSegment : group)
            groupedSegment.updateSlopes(aLowerGroup, aUpperGroup);

        if (groupedSegments.containsKey(group.getFirst().getMiddle().getA()))
            group.addAll(groupedSegments.get(group.getFirst().getMiddle().getA()));
        groupedSegments.put(group.getFirst().getMiddle().getA(), group);

        return groupedSegments;
    }
}
