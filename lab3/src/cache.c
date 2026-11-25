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
#define MIN_MEASURE_STEPS (1 * MB)
#define WARMUP 2
#define TRIALS 5
typedef struct
{
    double min;
    double avg;
    double median;
} stats_t;
static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
static uint64_t rng_state = 88172645463325252ULL;
static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static void shuffle(size_t *a, size_t n)
{
    for (size_t i = n - 1; i > 0; i--)
    {
        size_t j = rng() % (i + 1);
        size_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}
static void *alloc(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 16 * KB, bytes) != 0)
    {
        perror("posix_memalign");
        exit(1);
    }
    memset(p, 0, bytes);
    return p;
}
static volatile uint32_t sink;
static uint32_t chase_steps(const volatile uint32_t *a, uint32_t p, size_t steps)
{
    for (size_t i = 0; i < steps; i++)
        p = a[p];
    sink = p;
    return p;
}
static int compare_double(const void *lhs, const void *rhs)
{
    double a = *(const double *)lhs;
    double b = *(const double *)rhs;
    return (a > b) - (a < b);
}
static stats_t summarize(double *samples, size_t count)
{
    stats_t result = {samples[0], 0.0, 0.0};
    for (size_t i = 0; i < count; i++)
    {
        if (samples[i] < result.min)
            result.min = samples[i];
        result.avg += samples[i] / count;
    }
    qsort(samples, count, sizeof(*samples), compare_double);
    result.median = samples[count / 2];
    return result;
}
static stats_t measure_chase(const uint32_t *a, uint32_t start, size_t steps)
{
    double samples[TRIALS];
    for (int trial = 0; trial < TRIALS; trial++)
    {
        uint32_t p = start;
        for (int warm = 0; warm < WARMUP; warm++)
            p = chase_steps(a, p, steps);
        double t0 = now_ns();
        p = chase_steps(a, p, steps);
        samples[trial] = (now_ns() - t0) / steps;
        sink = p;
    }
    return summarize(samples, TRIALS);
}
static void walk_stride(int *arr, size_t n, size_t stride)
{
    size_t step = stride / sizeof(*arr);
    for (size_t i = 0; i < n; i += step)
        arr[i]++;
}
static stats_t measure_stride(int *arr, size_t n, size_t stride)
{
    double samples[TRIALS];
    for (int trial = 0; trial < TRIALS; trial++)
    {
        for (int warm = 0; warm < WARMUP; warm++)
            walk_stride(arr, n, stride);
        double t0 = now_ns();
        walk_stride(arr, n, stride);
        samples[trial] = now_ns() - t0;
    }
    return summarize(samples, TRIALS);
}
static size_t full_cycle_steps(size_t nodes)
{
    return nodes > MIN_MEASURE_STEPS ? nodes : MIN_MEASURE_STEPS;
}
static void link(uint32_t *a, const size_t *idx, size_t n)
{
    for (size_t i = 0; i < n; i++)
        a[idx[i]] = (uint32_t)idx[(i + 1) % n];
}
static void run_size(size_t step)
{
    printf("size_bytes,min_ns_per_access,avg_ns_per_access,median_ns_per_access\n");
    size_t max = 256 * MB;
    uint32_t *a = alloc(max);
    size_t *idx = malloc(max / step * sizeof(size_t));
    for (size_t p = 4 * KB; p <= max; p *= 2)
    {
        for (int q = 4; q < 8 && p * q / 4 <= max; q++)
        {
            size_t size = p * q / 4;
            size_t n = size / step;
            for (size_t i = 0; i < n; i++)
                idx[i] = i * (step / sizeof(uint32_t));
            shuffle(idx, n);
            link(a, idx, n);
            stats_t t = measure_chase(a, (uint32_t)idx[0], full_cycle_steps(n));
            printf("%zu,%.3f,%.3f,%.3f\n", size, t.min, t.avg, t.median);
            fflush(stdout);
        }
    }
    free(idx);
    free(a);
}
static void run_line(size_t bytes)
{
    printf("method,stride_bytes,min_value,avg_value,median_value\n");
    int *arr = alloc(bytes);
    size_t n = bytes / sizeof(int);
    for (size_t stride = 4; stride <= 4096; stride *= 2)
    {
        stats_t t = measure_stride(arr, n, stride);
        printf("A,%zu,%.3f,%.3f,%.3f\n", stride,
               t.min / 1e6, t.avg / 1e6, t.median / 1e6);
        fflush(stdout);
    }
    free(arr);
    size_t block = 256;
    size_t nblocks = bytes / block;
    uint32_t *a = alloc(bytes);
    size_t *idx = malloc(2 * nblocks * sizeof(size_t));
    size_t *order = malloc(nblocks * sizeof(size_t));
    static const size_t distances[] = {
        4, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80,
        88, 96, 104, 112, 120, 128, 136, 144, 160, 192};
    for (size_t i = 0; i < nblocks; i++)
        order[i] = i;
    shuffle(order, nblocks);
    for (size_t di = 0; di < sizeof(distances) / sizeof(distances[0]); di++)
    {
        size_t d = distances[di];
        for (size_t i = 0; i < nblocks; i++)
        {
            size_t base = order[i] * (block / sizeof(uint32_t));
            idx[2 * i] = base;
            idx[2 * i + 1] = base + d / sizeof(uint32_t);
        }
        link(a, idx, 2 * nblocks);
        stats_t t = measure_chase(a, (uint32_t)idx[0],
                                  full_cycle_steps(2 * nblocks));
        printf("B,%zu,%.3f,%.3f,%.3f\n", d,
               2 * t.min, 2 * t.avg, 2 * t.median);
        fflush(stdout);
    }
    free(order);
    free(idx);
    free(a);
}
static void run_assoc(size_t cache, int maxn)
{
    printf("fragments,min_ns_per_access,avg_ns_per_access,median_ns_per_access\n");
    uint32_t *a = alloc(cache * (maxn + 1));
    size_t *idx = malloc(2 * (size_t)maxn * sizeof(size_t));
    for (int nf = 1; nf <= maxn; nf++)
    {
        for (int f = 0; f < nf; f++)
        {
            idx[2 * f] = (size_t)f * cache / sizeof(uint32_t);
            idx[2 * f + 1] = idx[2 * f] + 1;
        }
        shuffle(idx, 2 * (size_t)nf);
        link(a, idx, 2 * (size_t)nf);
        stats_t t = measure_chase(a, (uint32_t)idx[0],
                                  full_cycle_steps(2 * (size_t)nf));
        printf("%d,%.3f,%.3f,%.3f\n", nf, t.min, t.avg, t.median);
        fflush(stdout);
    }
    free(idx);
    free(a);
}

int main(int argc, char **argv)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s size [step] | line [bytes] | assoc cache_kb [maxn]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "size") == 0)
    {
        run_size(argc > 2 ? strtoul(argv[2], 0, 10) : 64);
    }
    else if (strcmp(argv[1], "line") == 0)
    {
        run_line(argc > 2 ? strtoul(argv[2], 0, 10) * MB : 64 * MB);
    }
    else if (strcmp(argv[1], "assoc") == 0)
    {
        size_t cache = (argc > 2 ? strtoul(argv[2], 0, 10) : 128) * KB;
        int maxn = argc > 3 ? atoi(argv[3]) : 32;
        run_assoc(cache, maxn);
    }
    else
    {
        fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 1;
    }
    return 0;
}
