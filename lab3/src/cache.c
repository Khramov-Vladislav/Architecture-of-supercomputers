// Исследование характеристик кэш-памяти:
//   size  - размер каждого уровня кэша (случайный обход массива 4 КБ .. 256 МБ)
//   line  - размер строки кэша (обход массива с шагом stride)
//   assoc - степень ассоциативности (обход N фрагментов, отстоящих на размер кэша)
// Сборка: clang -O2 -o cache cache.c
// Запуск: ./cache size|line|assoc [параметры] > result.csv
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __APPLE__
#include <pthread.h>
#endif

#define KB (1024UL)
#define MB (1024UL * KB)

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static uint64_t rng_state = 88172645463325252ULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void shuffle(size_t *a, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = rng() % (i + 1);
        size_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static void *alloc(size_t bytes) {
    void *p = NULL;
    if (posix_memalign(&p, 16 * KB, bytes) != 0) {
        perror("posix_memalign");
        exit(1);
    }
    memset(p, 0, bytes);
    return p;
}

// Обход цепочки: каждый элемент хранит индекс следующего.
// Следующий адрес известен только после чтения текущего, поэтому
// процессор не может перекрыть обращения, а предвыборка не угадывает адрес.
static volatile uint32_t sink;

static double chase(const uint32_t *a, uint32_t start, size_t steps) {
    uint32_t p = start;
    for (size_t i = 0; i < steps / 10; i++)  // прогрев
        p = a[p];
    double best = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_ns();
        for (size_t i = 0; i < steps; i++)
            p = a[p];
        double t = (now_ns() - t0) / steps;
        if (t < best)
            best = t;
    }
    sink = p;
    return best;
}

// Связывает элементы с индексами idx[0..n-1] в цикл в заданном порядке.
static void link(uint32_t *a, const size_t *idx, size_t n) {
    for (size_t i = 0; i < n; i++)
        a[idx[i]] = (uint32_t)idx[(i + 1) % n];
}

// 1. Размер кэша: цепочка проходит по одному int из каждого блока
// размером step байт в случайном порядке.
static void run_size(size_t step) {
    printf("size_bytes,ns_per_access\n");
    size_t max = 256 * MB;
    uint32_t *a = alloc(max);
    size_t *idx = malloc(max / step * sizeof(size_t));
    // размеры 2^k, 1.25 * 2^k, 1.5 * 2^k, 1.75 * 2^k от 4 КБ до 256 МБ
    for (size_t p = 4 * KB; p <= max; p *= 2) {
        for (int q = 4; q < 8 && p * q / 4 <= max; q++) {
            size_t size = p * q / 4;
            size_t n = size / step;
            for (size_t i = 0; i < n; i++)
                idx[i] = i * (step / sizeof(uint32_t));
            shuffle(idx, n);
            link(a, idx, n);
            double t = chase(a, (uint32_t)idx[0], 20 * MB);
            printf("%zu,%.3f\n", size, t);
            fflush(stdout);
        }
    }
    free(idx);
    free(a);
}

// 2. Размер строки.
// Метод А: последовательный обход массива размера bytes с шагом stride,
// время на весь проход. Пока stride меньше строки, загружаются все строки
// массива и время почти не меняется; дальше оно падает вдвое на каждое
// удвоение шага.
// Метод Б: цепочка по случайно переставленным блокам по 4 КБ, в каждом
// блоке читаются два int на расстоянии d. Пока d меньше строки, оба
// элемента лежат в одной строке (1 промах на блок), иначе промахов два.
static void run_line(size_t bytes) {
    printf("method,stride_bytes,value\n");
    int *arr = alloc(bytes);
    size_t n = bytes / sizeof(int);
    for (size_t stride = 4; stride <= 4096; stride *= 2) {
        size_t s = stride / sizeof(int);
        double best = 1e30;
        for (int rep = 0; rep < 7; rep++) {
            double t0 = now_ns();
            for (size_t i = 0; i < n; i += s)
                arr[i]++;
            double t = now_ns() - t0;
            if (t < best)
                best = t;
        }
        printf("A,%zu,%.3f\n", stride, best / 1e6);  // мс на проход
        fflush(stdout);
    }
    free(arr);

    size_t block = 4 * KB;
    size_t nblocks = bytes / block;
    uint32_t *a = alloc(bytes);
    size_t *idx = malloc(2 * nblocks * sizeof(size_t));
    size_t *order = malloc(nblocks * sizeof(size_t));
    for (size_t d = 4; d <= 2048; d *= 2) {
        for (size_t i = 0; i < nblocks; i++)
            order[i] = i;
        shuffle(order, nblocks);
        // элементы блока: смещение 0 и смещение d
        for (size_t i = 0; i < nblocks; i++) {
            size_t base = order[i] * (block / sizeof(uint32_t));
            idx[2 * i] = base;
            idx[2 * i + 1] = base + d / sizeof(uint32_t);
        }
        link(a, idx, 2 * nblocks);
        double t = chase(a, (uint32_t)idx[0], 8 * MB);
        printf("B,%zu,%.3f\n", d, 2 * t);  // нс на один блок (два чтения)
        fflush(stdout);
    }
    free(order);
    free(idx);
    free(a);
}

// 3. Ассоциативность: N фрагментов, начала которых отстоят друг от друга
// на cache байт (кратно размеру одного «пути» кэша), поэтому j-е строки
// всех фрагментов попадают в один и тот же набор. Длина фрагмента
// cache / maxn, так что суммарный объём N фрагментов не превышает размер
// кэша. Пока N не больше степени ассоциативности, все строки помещаются
// в свои наборы; при N = k + 1 в каждом наборе не хватает места и
// циклический обход начинает промахиваться.
static void run_assoc(size_t cache, size_t step, int maxn) {
    printf("fragments,ns_per_access\n");
    uint32_t *a = alloc(cache * (maxn + 1));
    size_t *idx = malloc(cache / step * sizeof(size_t));
    for (int nf = 1; nf <= maxn; nf++) {
        size_t per = cache / maxn / step;
        size_t n = 0;
        for (int f = 0; f < nf; f++)
            for (size_t j = 0; j < per; j++)
                idx[n++] = (f * cache + j * step) / sizeof(uint32_t);
        shuffle(idx, n);
        link(a, idx, n);
        double t = chase(a, (uint32_t)idx[0], 20 * MB);
        printf("%d,%.3f\n", nf, t);
        fflush(stdout);
    }
    free(idx);
    free(a);
}

int main(int argc, char **argv) {
#ifdef __APPLE__
    // просим планировщик держать поток на производительных ядрах
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc < 2) {
        fprintf(stderr, "usage: %s size [step] | line [bytes] | assoc cache_kb [step] [maxn]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "size") == 0) {
        run_size(argc > 2 ? strtoul(argv[2], 0, 10) : 64);
    } else if (strcmp(argv[1], "line") == 0) {
        run_line(argc > 2 ? strtoul(argv[2], 0, 10) * MB : 64 * MB);
    } else if (strcmp(argv[1], "assoc") == 0) {
        size_t cache = (argc > 2 ? strtoul(argv[2], 0, 10) : 128) * KB;
        size_t step = argc > 3 ? strtoul(argv[3], 0, 10) : 64;
        int maxn = argc > 4 ? atoi(argv[4]) : 32;
        run_assoc(cache, step, maxn);
    } else {
        fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 1;
    }
    return 0;
}
