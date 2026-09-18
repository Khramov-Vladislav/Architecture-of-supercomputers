package src.main.java.org.example;

public class Bf16 {
    private int bits;

    // перевод дробной части в двоичную сс
    private static String decimalToBinaryFraction(double frac, int precision) {
        StringBuilder binary = new StringBuilder();
        int countPrecision = 0;

        while (frac != 0 && countPrecision != precision) {
            frac *= 2;
            if (frac >= 1) {
                binary.append(1);
                frac -= 1;
            } else {
                binary.append(0);
            }
            countPrecision++;
        }

        while (countPrecision != precision) {
            binary.append(0);
            countPrecision++;
        }
        return binary.toString();
    }

    // перевод целой части в двоичную сс
    private static String decimalToBinaryInteger(int num) {
        StringBuilder binary = new StringBuilder();

        if (num == 0) {
            return "0";
        }

        while (num > 0) {
            int remainder = num % 2;
            num /= 2;
            binary.append(remainder);
        }

        return binary.reverse().toString();
    }

    // округление мантиссы до targetBits бит по правилу round-to-nearest-even
    private static Rounded roundMantissa(String mantissa, int exponent, int targetBits) {
        StringBuilder padded = new StringBuilder(mantissa);
        while (padded.length() < targetBits + 1) {
            padded.append('0');
        }

        String kept = padded.substring(0, targetBits);
        char guardBit = padded.charAt(targetBits);
        String remainder = padded.length() > targetBits + 1 ? padded.substring(targetBits + 1) : "";

        boolean roundUp;
        if (guardBit == '0') {
            roundUp = false;
        } else if (remainder.contains("1")) {
            roundUp = true;
        } else {
            char lastKeptBit = kept.charAt(kept.length() - 1);
            roundUp = (lastKeptBit == '1');
        }

        int keptValue = kept.isEmpty() ? 0 : Integer.parseInt(kept, 2);
        if (roundUp) {
            keptValue += 1;
        }

        int maxValue = (1 << targetBits) - 1;
        if (keptValue > maxValue) {
            keptValue = 0;
            exponent += 1;
        }

        String mantissaBits = toFixedLengthBinary(keptValue, targetBits);
        return new Rounded(exponent, mantissaBits);
    }

    private static String toFixedLengthBinary(int value, int length) {
        StringBuilder sb = new StringBuilder(Integer.toBinaryString(value));
        while (sb.length() < length) {
            sb.insert(0, '0');
        }
        return sb.toString();
    }

    // перевод мантиссы (как битов) обратно в десятичную дробь: сумма bit_i * 2^-(i+1)
    private static double mantissaToDecimal(int mantissaBits, int bitCount) {
        double value = 0.0;
        for (int i = 0; i < bitCount; i++) {
            int bit = (mantissaBits >> (bitCount - 1 - i)) & 1;
            value += bit * Math.pow(2, -(i + 1));
        }
        return value;
    }

    // кодирование
    public static int encode(double value) {
        int sign = (value < 0) ? 1 : 0;
        double absValue = Math.abs(value);

        if (Double.isNaN(value)) {
            return (sign << 15) | (0xFF << 7) | 0x40; // экспонента=255, мантисса!=0
        }
        if (Double.isInfinite(value)) {
            return (sign << 15) | (0xFF << 7);
        }
        if (absValue == 0.0) {
            return sign << 15; // E=0, M=0
        }

        int intPart = (int) absValue;
        double fracPart = absValue - intPart;

        String binaryInteger = decimalToBinaryInteger(intPart);
        String binaryFraction = decimalToBinaryFraction(fracPart, 32); // запас точности

        Normalized norm = normalize(binaryInteger, binaryFraction);
        Rounded rounded = roundMantissa(norm.mantissa(), norm.exponent(), 7);

        int biasedExponent = rounded.exponent() + 127;

        if (biasedExponent >= 255) {
            return (sign << 15) | (0xFF << 7); // переполнение -> бесконечность
        }
        if (biasedExponent <= 0) {
            return sign << 15; // антипереполнение -> упрощённо считаем нулём
        }

        int mantissaBits = Integer.parseInt(rounded.mantissa(), 2);

        return (sign << 15) | (biasedExponent << 7) | mantissaBits;
    }

    // декодирование
    public static double decode(int bits) {
        bits = bits & 0xFFFF;

        int sign = (bits >> 15) & 0x1;
        int exponent = (bits >> 7) & 0xFF;
        int mantissa = bits & 0x7F;

        double signMultiplier = (sign == 0) ? 1.0 : -1.0;

        if (exponent == 0 && mantissa == 0) {
            return signMultiplier * 0.0;
        }
        if (exponent == 255) {
            return (mantissa == 0)
                    ? signMultiplier * Double.POSITIVE_INFINITY
                    : Double.NaN;
        }
        if (exponent == 0) {
            double mantissaValue = mantissaToDecimal(mantissa, 7);
            return signMultiplier * mantissaValue * Math.pow(2, -126);
        }

        double mantissaValue = mantissaToDecimal(mantissa, 7);
        int realExponent = exponent - 127;

        return signMultiplier * (1 + mantissaValue) * Math.pow(2, realExponent);
    }

    // вывод
    public static String toBinaryString(int bits) {
        bits = bits & 0xFFFF;
        String full = toFixedLengthBinary(bits, 16);
        return full.charAt(0) + " " + full.substring(1, 9) + " " + full.substring(9);
    }

    // нормализация, приведение числа к виду 1,..... * 2^e
    private static Normalized normalize(String binary, String binaryFraction) {
        String combined = binary + binaryFraction;
        int pointPosition = binary.length();

        int firstOne = combined.indexOf('1');
        if (firstOne == -1) {
            throw new IllegalArgumentException("Число равно нулю, нормализация невозможна");
        }

        int exponent = pointPosition - firstOne - 1;
        String mantissa = combined.substring(firstOne + 1);

        return new Normalized(exponent, mantissa);
    }

    private record Normalized(int exponent, String mantissa) {
    }

    private record Rounded(int exponent, String mantissa) {
    }

}
