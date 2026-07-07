package io.github.xkitsios;

public class DoubleEncoder {
    public static int write(double number, byte[] buffer, int pos) {
        long longBits = Double.doubleToLongBits(number);
        for (int i = 0; i < 8; i++)
            buffer[pos++] = (byte) (longBits >>> (i * 8));

        return pos;
    }

    public static double read(byte[] buffer, int[] offsetHolder) {
        long value = 0L;

        for (int i = 0; i < 8; i++)
            value |= ((long) (buffer[offsetHolder[0]++] & 0xFF)) << (i * 8);

        return Double.longBitsToDouble(value);
    }
}
