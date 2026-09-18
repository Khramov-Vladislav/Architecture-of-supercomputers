package src.test.java.org.example;

import org.junit.jupiter.api.Test;
import src.main.java.org.example.Bf16;

import static org.junit.jupiter.api.Assertions.*;

public class Bf16Test {

    private static final double DELTA = 1e-9;

    // ==================== Точно представимые значения ====================
    // Эти числа не теряют точность в BF16 (мантисса умещается в 7 бит),
    // поэтому проверяем decode(encode(x)) == x без допуска на погрешность.

    @Test
    void testPositiveInteger() {
        int bits = Bf16.encode(3.0);
        assertEquals("0 10000000 1000000", Bf16.toBinaryString(bits));
        assertEquals(3.0, Bf16.decode(bits), DELTA);
    }

    @Test
    void testFractionExact() {
        int bits = Bf16.encode(5.25);
        assertEquals("0 10000001 0101000", Bf16.toBinaryString(bits));
        assertEquals(5.25, Bf16.decode(bits), DELTA);
    }

    @Test
    void testOne() {
        int bits = Bf16.encode(1.0);
        assertEquals("0 01111111 0000000", Bf16.toBinaryString(bits));
        assertEquals(1.0, Bf16.decode(bits), DELTA);
    }

    @Test
    void testQuarter() {
        int bits = Bf16.encode(0.25);
        assertEquals(0.25, Bf16.decode(bits), DELTA);
    }

    @Test
    void testLargePowerOfTwo() {
        // Проверяем, что диапазон BF16 такой же широкий, как у float32,
        // а не ограничен размером в 16 бит (2^16 = 65536 представляется точно)
        int bits = Bf16.encode(65536.0);
        assertEquals(65536.0, Bf16.decode(bits), DELTA);
    }

    // ==================== Знак ====================

    @Test
    void testNegativeNumber() {
        int bits = Bf16.encode(-2.5);
        assertEquals("1 10000000 0100000", Bf16.toBinaryString(bits));
        assertEquals(-2.5, Bf16.decode(bits), DELTA);
    }

    @Test
    void testNegativeFraction() {
        int bits = Bf16.encode(-0.5);
        assertEquals(-0.5, Bf16.decode(bits), DELTA);
    }

    // ==================== Ноль ====================

    @Test
    void testPositiveZero() {
        int bits = Bf16.encode(0.0);
        assertEquals("0 00000000 0000000", Bf16.toBinaryString(bits));
        assertEquals(0.0, Bf16.decode(bits), DELTA);
    }

    // ==================== Потеря точности (мантиссы не хватает) ====================

    @Test
    void testPrecisionLossOnOneTenth() {
        // 0.1 не представимо точно ни в float, ни тем более в BF16 с его 7 битами мантиссы.
        // Проверяем, что результат близок, но не идентичен исходному значению.
        int bits = Bf16.encode(0.1);
        double decoded = Bf16.decode(bits);

        assertEquals(0.10009765625, decoded, DELTA);
        assertNotEquals(0.1, decoded); // подтверждаем, что точность действительно потеряна
    }

    // ==================== Округление до чётного (round-to-nearest-even) ====================

    @Test
    void testRoundToEven_TieRoundsDownWhenKeptBitIsEven() {
        // Нормализованная мантисса: 1.00000001 (ровно посередине между
        // 1.0000000 и 1.0000001). Последний оставляемый бит = 0 (чётный) -> округление ВНИЗ.
        double value = 1.00390625; // 1 + 2^-8
        int bits = Bf16.encode(value);

        assertEquals(1.0, Bf16.decode(bits), DELTA);
    }

    @Test
    void testRoundToEven_TieRoundsUpWhenKeptBitIsOdd() {
        // Нормализованная мантисса: 1.00000011 (ровно посередине между
        // 1.0000001 и 1.0000010). Последний оставляемый бит = 1 (нечётный) -> округление ВВЕРХ.
        double value = 1.01171875; // 1 + 3/256
        int bits = Bf16.encode(value);

        assertEquals(1.015625, Bf16.decode(bits), DELTA);
    }

    // ==================== Специальные значения ====================

    @Test
    void testPositiveInfinity() {
        int bits = Bf16.encode(Double.POSITIVE_INFINITY);
        assertTrue(Double.isInfinite(Bf16.decode(bits)));
        assertTrue(Bf16.decode(bits) > 0);
    }

    @Test
    void testNegativeInfinity() {
        int bits = Bf16.encode(Double.NEGATIVE_INFINITY);
        assertTrue(Double.isInfinite(Bf16.decode(bits)));
        assertTrue(Bf16.decode(bits) < 0);
    }

    @Test
    void testNaN() {
        int bits = Bf16.encode(Double.NaN);
        assertTrue(Double.isNaN(Bf16.decode(bits)));
    }

    // ==================== Переполнение диапазона ====================
//
//    @Test
//    void testOverflowToInfinity() {
//        // Число за пределами диапазона экспоненты BF16 (примерно 3.39 * 10^38)
//        // должно "сгореть" в бесконечность, а не выдать мусорный результат.
//        int bits = Bf16.encode(1e40);
//        assertTrue(Double.isInfinite(Bf16.decode(bits)));
//    }
//
//    @Test
//    void testUnderflowToZero() {
//        // Слишком маленькое число (за пределами минимальной экспоненты)
//        // в текущей упрощённой реализации сворачивается в ноль.
//        int bits = Bf16.encode(1e-40);
//        assertEquals(0.0, Bf16.decode(bits), DELTA);
//    }

    // ==================== Формат вывода битов ====================

    @Test
    void testBinaryStringFormat() {
        int bits = Bf16.encode(3.0);
        String formatted = Bf16.toBinaryString(bits);

        // Формат: "S EEEEEEEE MMMMMMM" -> длина 1 + 1(пробел) + 8 + 1(пробел) + 7 = 18
        assertEquals(18, formatted.length());
        assertEquals("0 10000000 1000000", formatted);
    }
}