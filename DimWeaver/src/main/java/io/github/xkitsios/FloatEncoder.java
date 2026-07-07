package io.github.xkitsios;

public class FloatEncoder {
    public static int write(float number, byte[] buffer, int pos) {
        int intBits = Float.floatToIntBits(number);
        for (int i = 0; i < 4; i++)
            buffer[pos++] = (byte) (intBits >>> (i * 8));

        return pos;
    }

    public static float read(byte[] buffer, int[] offsetHolder) {
        int value = 0;

        for (int i = 0; i < 4; i++)
            value |= (buffer[offsetHolder[0]++] & 0xFF) << (i * 8);

        return Float.intBitsToFloat(value);
    }
}
