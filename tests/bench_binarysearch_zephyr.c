/*
 * bench_binarysearch_zephyr.c  —  Binary Search RT Benchmark  (Zephyr RTOS target)
 * ==================================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration binary search execution time and jitter.
 *           Each iteration performs SEARCHES_PER_ITER independent
 *           binary searches on a sorted array of ARRAY_SIZE int32 elements.
 *
 * Build (west, target rpi_4b):
 *   Add this file to CMakeLists.txt sources and run:
 *   west build -b rpi_4b .
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS        1000
#define ARRAY_SIZE        65536   /* 64 K sorted int32 elements       */
#define SEARCHES_PER_ITER 1024    /* independent searches per timing  */

/* ------------------------------------------------------------------ */
/*  Static data arrays                                                 */
/* ------------------------------------------------------------------ */
static int32_t  s_arr[ARRAY_SIZE];
static int32_t  s_targets[SEARCHES_PER_ITER];
static uint64_t s_exec_ns[ITERATIONS];

/* ------------------------------------------------------------------ */
/*  Binary search: returns index or -1                                 */
/* ------------------------------------------------------------------ */
static int32_t bsearch_i32(const int32_t *arr, int32_t len, int32_t key) {
    int32_t lo = 0, hi = len - 1;
    while (lo <= hi) {
        int32_t mid = lo + ((hi - lo) >> 1);
        if (arr[mid] == key) return mid;
        if (arr[mid] < key)  lo = mid + 1;
        else                 hi = mid - 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Timing helper  (Zephyr hardware cycle counter)                    */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    return k_cyc_to_ns_near64(k_cycle_get_64());
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printk("=== RT-Bench: BINARYSEARCH  [Zephyr] ===\n");
    printk("Iterations       : %d\n", ITERATIONS);
    printk("Array size       : %d int32 elements\n", ARRAY_SIZE);
    printk("Searches per iter: %d\n\n", SEARCHES_PER_ITER);

    /* Build sorted array: values 0, 2, 4, ... (every even number) */
    for (int i = 0; i < ARRAY_SIZE; i++) s_arr[i] = i * 2;

    /* Build search targets spread across the range */
    for (int i = 0; i < SEARCHES_PER_ITER; i++)
        s_targets[i] = (int32_t)(((long long)i * (ARRAY_SIZE * 2)) / SEARCHES_PER_ITER);

    volatile int32_t sink = 0;  /* prevent dead-code elimination */

    /* Warm-up */
    for (int w = 0; w < 5; w++)
        for (int s = 0; s < SEARCHES_PER_ITER; s++)
            sink += bsearch_i32(s_arr, ARRAY_SIZE, s_targets[s]);

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        for (int s = 0; s < SEARCHES_PER_ITER; s++)
            sink += bsearch_i32(s_arr, ARRAY_SIZE, s_targets[s]);
        s_exec_ns[i] = now_ns() - t0;
    }

    /* Statistics */
    uint64_t sum = 0, min_ns = UINT64_MAX, max_ns = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum += s_exec_ns[i];
        if (s_exec_ns[i] < min_ns) min_ns = s_exec_ns[i];
        if (s_exec_ns[i] > max_ns) max_ns = s_exec_ns[i];
    }
    uint64_t avg_ns = sum / ITERATIONS;
    uint64_t jitter = max_ns - min_ns;

    printk("Result sink (anti-DCE): %d\n\n", (int)sink);

    printk("%-28s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printk("%-28s %10llu %10llu %10llu %10llu\n",
           "BINARYSEARCH [Zephyr]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    return 0;
}
