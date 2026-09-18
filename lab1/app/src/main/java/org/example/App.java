package src.main.java.org.example;

import java.util.Scanner;

public class App {
    private static Scanner scanner = new Scanner(System.in);

    public static void main(String[] args) {
        while (true) {
            menu();
            int choice = getValidInt("Ваш выбор: ", 0, 2);

            switch (choice) {
                case 0: {
                    System.out.println("Выход из программы.");
                    return;
                }
                case 1: {
                    String bits = getValidBits("Введите число в двоичной системе счисления (16 бит):");
                    int rawBits = Integer.parseInt(bits, 2);
                    double decoded = Bf16.decode(rawBits);
                    System.out.println("Результат (десятичное число): " + decoded);
                    break;
                }
                case 2: {
                    double value = getValidDouble("Введите десятичное число:");
                    int encoded = Bf16.encode(value);
                    System.out.println("Результат (BF16): " + Bf16.toBinaryString(encoded));
                    break;
                }
                default: {
                    System.out.println("Некорректный выбор");
                    break;
                }
            }

            System.out.println();
        }
    }

    private static void menu() {
        System.out.println("Меню");
        System.out.println("1. Перевод из двоичной системы счисления в десятичную");
        System.out.println("2. Перевод из десятичной системы счисления в двоичную");
        System.out.println("0. Выход");
    }

    private static int getValidInt(String message, int min, int max) {
        System.out.println(message);
        int value;
        while (true) {
            if (scanner.hasNextInt()) {
                value = scanner.nextInt();
                if (value < min || value > max) {
                    System.out.printf("Введите число в диапазоне от %d до %d%n", min, max);
                    continue;
                }
                break;
            }
            scanner.next();
            System.out.println("Некорректный символ");
        }

        return value;
    }

    private static double getValidDouble(String message) {
        System.out.println(message);
        double value;
        while (true) {
            if (scanner.hasNextDouble()) {
                value = scanner.nextDouble();
                break;
            }
            scanner.next();
            System.out.println("Некорректное число, попробуйте снова");
        }

        return value;
    }

    private static String getValidBits(String message) {
        System.out.println(message);

        while (true) {
            String bits = scanner.next();
            boolean valid = true;

            if (bits.length() != 16) {
                System.out.println("Число должно состоять ровно из 16 бит");
                valid = false;
            } else {
                for (byte b : bits.getBytes()) {
                    if (b != '1' && b != '0') {
                        System.out.println("Число должно состоять только из 0 и 1");
                        valid = false;
                        break;
                    }
                }
            }

            if (valid) {
                return bits;
            }
        }
    }

}