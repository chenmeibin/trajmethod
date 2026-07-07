package io.github.xkitsios;

import me.lemire.integercompression.*;

import java.io.IOException;
import java.util.*;

public class Binary {
    private static final int MIN_TIMESTAMPS_THRESHOLD = 128;
    private static final IntegerCODEC CODEC_FastPFOR128 = new Composition(new FastPFOR128(), new VariableByte());
    private static final IntegerCODEC CODEC_Simple16 = new Composition(new Simple16(), new VariableByte());

    private static int dimensionWriter(boolean[] dimensions, byte[] buffer, int pos) {
        int dimBytes = (int) Math.ceil((double) dimensions.length / 8);
        pos = VariableByteEncoder.write(dimBytes, buffer, pos);
        for (int i = 0; i < dimensions.length; i++) {
            if (dimensions[i]) {
                int byteIndex = i / 8;
                int bitIndex = i % 8;
                buffer[pos + byteIndex] |= (byte) (1 << bitIndex);
            }
        }
        pos += dimBytes;

        return pos;
    }

    private static boolean[] dimensionReader(byte[] binary, int[] pos) {
        int dimensionBytes = Math.toIntExact(VariableByteEncoder.read(binary, pos));
        boolean[] dimensions = new boolean[dimensionBytes * 8];
        for (int i = 0; i < dimensions.length; i++) {
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            dimensions[i] = ((binary[pos[0] + byteIndex] >> bitIndex) & 1) == 1;
        }
        pos[0] += dimensionBytes;

        return dimensions;
    }

    private static int countersWriter(int[] counters, byte[] buffer, int pos) {
        int[] compressed = new int[counters.length + 1];
        IntWrapper outputOffset = new IntWrapper(0);
        CODEC_Simple16.compress(counters, new IntWrapper(0), counters.length, compressed, outputOffset);
        pos = VariableByteEncoder.write(outputOffset.intValue(), buffer, pos);
        pos = VariableByteEncoder.write(compressed[0], buffer, pos);
        for (int i = 1; i < outputOffset.intValue(); i++) {
            int val = compressed[i];
            buffer[pos++] = (byte) (val >>> 24);
            buffer[pos++] = (byte) (val >>> 16);
            buffer[pos++] = (byte) (val >>> 8);
            buffer[pos++] = (byte) val;
        }

        return pos;
    }

    private static int[] countersReader(byte[] buffer, int[] pos) {
        int compressedInt = Math.toIntExact(VariableByteEncoder.read(buffer, pos));
        int uncompressedInt = Math.toIntExact(VariableByteEncoder.read(buffer, pos));
        int[] intArray = new int[uncompressedInt + 1];
        intArray[0] = uncompressedInt;
        for (int i = 1; i < compressedInt; i++)
            intArray[i] = ((buffer[pos[0]++] & 0xFF) << 24) | ((buffer[pos[0]++] & 0xFF) << 16) | ((buffer[pos[0]++] & 0xFF) << 8) | (buffer[pos[0]++] & 0xFF);
        int[] counters = new int[intArray[0]];
        IntWrapper recOffset = new IntWrapper(0);
        CODEC_Simple16.uncompress(intArray, new IntWrapper(0), intArray.length, counters, recOffset);

        return counters;
    }

    private static int timestampsWriter(int[] timestamps, byte[] buffer, int pos){
        int[] compressed = new int[timestamps.length + 1];
        IntWrapper outputOffset = new IntWrapper(0);
        CODEC_FastPFOR128.compress(timestamps, new IntWrapper(0), timestamps.length, compressed, outputOffset);
        pos = VariableByteEncoder.write(outputOffset.intValue(), buffer, pos);
        for (int i = 0; i < outputOffset.intValue(); i++) {
            int val = compressed[i];
            buffer[pos++] = (byte) (val >>> 24);
            buffer[pos++] = (byte) (val >>> 16);
            buffer[pos++] = (byte) (val >>> 8);
            buffer[pos++] = (byte) val;
        }

        return pos;
    }

    private static int[] timestampsReader(byte[] buffer, int[] pos, int totalTimestamps){
        int compressedInt = Math.toIntExact(VariableByteEncoder.read(buffer, pos));
        int[] intArray = new int[compressedInt];
        for (int i = 0; i < compressedInt; i++)
            intArray[i] = ((buffer[pos[0]++] & 0xFF) << 24) | ((buffer[pos[0]++] & 0xFF) << 16) | ((buffer[pos[0]++] & 0xFF) << 8) | (buffer[pos[0]++] & 0xFF);
        int[] timestamps = new int[totalTimestamps];
        IntWrapper recOffset = new IntWrapper(0);
        CODEC_FastPFOR128.uncompress(intArray, new IntWrapper(0), intArray.length, timestamps, recOffset);

        return timestamps;
    }

    public static byte[] encode(Map<Double, List<Segment>> groupedSegments, long smallestT) {
        int totalSegments = groupedSegments.values().stream().mapToInt(List::size).sum();

        boolean vb = totalSegments < MIN_TIMESTAMPS_THRESHOLD;
        int[] timestamps = new int[totalSegments];
        int timestampsIdx = 0;

        // Dimension metadata
        boolean[] dimensions = new boolean[totalSegments];
        int dimIdx = 0;

        //Counters metadata
        int[] counters = new int[groupedSegments.size()];
        int countersIdx = 0;

        byte[] groupsBuffer = new byte[groupedSegments.size() * 4 + totalSegments * (8 + 8)];
        int groupsBufferPos = 0;
        for (List<Segment> segments : groupedSegments.values()) {
            counters[countersIdx++] = segments.size();
            groupsBufferPos = FloatEncoder.write((float) segments.getFirst().getMiddle().getA(), groupsBuffer, groupsBufferPos);
            segments.sort(Comparator.comparingLong(Segment::getStart));
            long previousT = 0L;
            for (Segment segment : segments) {
                groupsBufferPos = DoubleEncoder.write(segment.getMiddle().getB(), groupsBuffer, groupsBufferPos);
                if (vb)
                    groupsBufferPos = VariableByteEncoder.write(segment.getStart() - previousT - smallestT, groupsBuffer, groupsBufferPos);
                else
                    timestamps[timestampsIdx++] = (int) (segment.getStart() - previousT - smallestT);
                previousT = segment.getStart() - smallestT;
                dimensions[dimIdx++] = segment.getDim();
            }
        }

        // Create binary
        byte[] buffer = new byte[(int) (8 + 8 + Math.ceil((double) dimensions.length / 8) + totalSegments * 4 + 1)];
        int pos = 0;

        pos = VariableByteEncoder.write(smallestT, buffer, pos);
        pos = dimensionWriter(dimensions, buffer, pos);
        pos = countersWriter(counters, buffer, pos);
        if (!vb)
            pos = timestampsWriter(timestamps, buffer, pos);
        byte[] output = new byte[groupsBufferPos + pos];
        System.arraycopy(buffer, 0, output, 0, pos);
        System.arraycopy(groupsBuffer, 0, output, pos, groupsBufferPos);

        return output;
    }

    public static List<List<Line>> decode(byte[] binary) throws IOException {
        int[] pos = {0};
        List<Line> linesX = new ArrayList<>();
        List<Line> linesY = new ArrayList<>();
        int countersIdx = 0, dimIdx = 0;

        long smallestT = VariableByteEncoder.read(binary, pos);
        boolean[] dimensions = dimensionReader(binary, pos);
        int[] counters = countersReader(binary, pos);
        int totalTimestamps = 0;
        for (int counter : counters)
            totalTimestamps += counter;
        boolean vb = totalTimestamps < MIN_TIMESTAMPS_THRESHOLD;
        int[] timestamps = null;
        if (!vb)
            timestamps = timestampsReader(binary, pos, totalTimestamps);
        int timestampsIdx = 0;

        List<int[]> subgroupsX = new ArrayList<>();
        List<int[]> subgroupsY = new ArrayList<>();

        while (pos[0] < binary.length) {
            double a = FloatEncoder.read(binary, pos);
            long previousT = 0L;

            int startX = linesX.size();
            int startY = linesY.size();

            for (int i = 0; i < counters[countersIdx]; i++) {
                double b = DoubleEncoder.read(binary, pos);
                long t;
                if (vb)
                    t = VariableByteEncoder.read(binary, pos) + previousT + smallestT;
                else
                    t = timestamps[timestampsIdx++] + previousT + smallestT;

                if (!dimensions[dimIdx])
                    linesX.add(new Line(t, a, b));
                else
                    linesY.add(new Line(t, a, b));
                dimIdx++;

                previousT = t - smallestT;
            }

            int endX = linesX.size();
            int endY = linesY.size();
            subgroupsX.add(new int[]{startX, endX});
            subgroupsY.add(new int[]{startY, endY});

            countersIdx++;
        }

        // Merge using k-way merge
        linesX = mergeKWay(linesX, subgroupsX);
        linesY = mergeKWay(linesY, subgroupsY);

        return Arrays.asList(linesX, linesY);
    }

    private static List<Line> mergeKWay(List<Line> lines, List<int[]> subgroups) {
        List<Line> result = new ArrayList<>(lines.size());
        PriorityQueue<Entry> pq = new PriorityQueue<>(Comparator.comparingDouble(e -> e.t));

        for (int i = 0; i < subgroups.size(); i++) {
            int[] range = subgroups.get(i);
            if (range[0] < range[1]) {
                Line line = lines.get(range[0]);
                pq.add(new Entry(line.getTStart(), i, range[0], range[1]));
            }
        }

        while (!pq.isEmpty()) {
            Entry e = pq.poll();
            result.add(lines.get(e.idx));
            int nextIdx = e.idx + 1;
            if (nextIdx < e.end) {
                e.idx = nextIdx;
                e.t = lines.get(nextIdx).getTStart();
                pq.add(e);
            }
        }

        return result;
    }

    private static class Entry {
        double t;
        int groupIndex;
        int idx;
        int end;

        Entry(double t, int groupIndex, int idx, int end) {
            this.t = t;
            this.groupIndex = groupIndex;
            this.idx = idx;
            this.end = end;
        }
    }
}