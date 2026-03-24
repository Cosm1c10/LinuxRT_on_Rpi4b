/*
 * bench_binarysearch_qnx.c  —  Binary Search RT Benchmark  (QNX Neutrino target)
 * ================================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration binary search execution time and jitter.
 *           Each iteration performs SEARCHES_PER_ITER independent
 *           binary searches on a sorted array of ARRAY_SIZE int32 elements.
 *
 * Build (on host, cross-compile for RPi 4):
 *   qcc -V gcc_ntoaarch64le -D_QNX_SOURCE -O2 \
 *       -o bench_binarysearch_qnx tests/bench_binarysearch_qnx.c
 *
 * Deploy & Run:
 *   scp bench_binarysearch_qnx qnxuser@<RPI_IP>:/tmp/
 *   ssh qnxuser@<RPI_IP> "/tmp/bench_binarysearch_qnx"
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS        1000
#define ARRAY_SIZE        65536   /* 64 K sorted int32 elements       */
#define SEARCHES_PER_ITER 1024    /* independent searches per timing  */

/* ------------------------------------------------------------------ */
/*  Data arrays                                                        */
/* ------------------------------------------------------------------ */
static int32_t s_arr[ARRAY_SIZE];

/* Search targets — evenly distributed across the value range */
static int32_t s_targets[SEARCHES_PER_ITER];

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
/*  Timing helper                                                      */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printf("=== RT-Bench: BINARYSEARCH  [QNX] ===\n");
    printf("Iterations       : %d\n", ITERATIONS);
    printf("Array size       : %d int32 elements\n", ARRAY_SIZE);
    printf("Searches per iter: %d\n\n", SEARCHES_PER_ITER);

    /* Build sorted array: values 0, 2, 4, ... (every even number) */
    for (int i = 0; i < ARRAY_SIZE; i++) s_arr[i] = i * 2;

    /* Build search targets spread across the range */
    for (int i = 0; i < SEARCHES_PER_ITER; i++)
        s_targets[i] = (int32_t)(((long long)i * (ARRAY_SIZE * 2)) / SEARCHES_PER_ITER);

    uint64_t *exec_ns = (uint64_t *)malloc(ITERATIONS * sizeof(uint64_t));
    if (!exec_ns) { fprintf(stderr, "malloc failed\n"); return 1; }

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
        exec_ns[i] = now_ns() - t0;
    }

    /* Statistics */
    uint64_t sum = 0, min_ns = UINT64_MAX, max_ns = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum += exec_ns[i];
        if (exec_ns[i] < min_ns) min_ns = exec_ns[i];
        if (exec_ns[i] > max_ns) max_ns = exec_ns[i];
    }
    uint64_t avg_ns = sum / ITERATIONS;
    uint64_t jitter = max_ns - min_ns;

    printf("Result sink (anti-DCE): %d\n\n", (int)sink);

    printf("%-26s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printf("%-26s %10llu %10llu %10llu %10llu\n",
           "BINARYSEARCH [QNX]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    free(exec_ns);
    return 0;
}
