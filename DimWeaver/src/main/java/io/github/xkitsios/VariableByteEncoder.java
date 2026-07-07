package io.github.xkitsios;

public class VariableByteEncoder {
    public static int write(long number, byte[] buffer, int pos) {
        while ((number & ~0x7FL) != 0) {
            buffer[pos++] = (byte) (number & 0x7F);
            number >>>= 7;
        }
        buffer[pos++] = (byte) (number | 0x80);

        return pos; // new offset
    }

    public static long read(byte[] buffer, int[] offsetHolder) {
        long result = 0;
        int shift = 0;
        byte b;

        while (true) {
            b = buffer[offsetHolder[0]++];
            result |= (long) (b & 0x7F) << shift;
            if ((b & 0x80) != 0) break;
            shift += 7;
        }

        return result;
    }
}
