/*
 * bench_fir2dim_qnx.c  —  2-D FIR Filter RT Benchmark  (QNX Neutrino target)
 * ============================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration 2-D FIR convolution execution time and jitter.
 *           Each iteration convolves a 256x256 int16 image with a 5x5
 *           integer kernel (no floating point required).
 *
 * Build (on host, cross-compile for RPi 4):
 *   qcc -V gcc_ntoaarch64le -D_QNX_SOURCE -O2 \
 *       -o bench_fir2dim_qnx tests/bench_fir2dim_qnx.c
 *
 * Deploy & Run:
 *   scp bench_fir2dim_qnx qnxuser@<RPI_IP>:/tmp/
 *   ssh qnxuser@<RPI_IP> "/tmp/bench_fir2dim_qnx"
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS    1000
#define IMG_ROWS      256
#define IMG_COLS      256
#define KERNEL_SIZE   5          /* 5x5 kernel                        */
#define KERNEL_HALF   2          /* floor(KERNEL_SIZE / 2)            */

/* ------------------------------------------------------------------ */
/*  5x5 low-pass (Gaussian-like) kernel  — integer weights            */
/* ------------------------------------------------------------------ */
static const int16_t K5[KERNEL_SIZE][KERNEL_SIZE] = {
    { 1,  4,  6,  4, 1 },
    { 4, 16, 24, 16, 4 },
    { 6, 24, 36, 24, 6 },
    { 4, 16, 24, 16, 4 },
    { 1,  4,  6,  4, 1 }
};
/* Sum of all kernel weights = 256 → divide output by 256 (>> 8) */
#define KERNEL_SUM_LOG2  8

/* ------------------------------------------------------------------ */
/*  Image buffers                                                      */
/* ------------------------------------------------------------------ */
static int16_t s_src[IMG_ROWS][IMG_COLS];
static int16_t s_dst[IMG_ROWS][IMG_COLS];

/* ------------------------------------------------------------------ */
/*  2-D FIR convolution (valid region; border pixels left as-is)      */
/* ------------------------------------------------------------------ */
static void fir2dim(void) {
    for (int r = KERNEL_HALF; r < IMG_ROWS - KERNEL_HALF; r++) {
        for (int c = KERNEL_HALF; c < IMG_COLS - KERNEL_HALF; c++) {
            int32_t acc = 0;
            for (int kr = 0; kr < KERNEL_SIZE; kr++)
                for (int kc = 0; kc < KERNEL_SIZE; kc++)
                    acc += (int32_t)K5[kr][kc] * (int32_t)s_src[r + kr - KERNEL_HALF][c + kc - KERNEL_HALF];
            s_dst[r][c] = (int16_t)(acc >> KERNEL_SUM_LOG2);
        }
    }
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
    printf("=== RT-Bench: FIR2DIM  [QNX] ===\n");
    printf("Iterations  : %d\n", ITERATIONS);
    printf("Image size  : %dx%d int16\n", IMG_ROWS, IMG_COLS);
    printf("Kernel size : %dx%d\n\n", KERNEL_SIZE, KERNEL_SIZE);

    /* Initialise source image with ramp data */
    for (int r = 0; r < IMG_ROWS; r++)
        for (int c = 0; c < IMG_COLS; c++)
            s_src[r][c] = (int16_t)((r * IMG_COLS + c) & 0xFF);

    uint64_t *exec_ns = (uint64_t *)malloc(ITERATIONS * sizeof(uint64_t));
    if (!exec_ns) { fprintf(stderr, "malloc failed\n"); return 1; }

    /* Warm-up */
    for (int w = 0; w < 3; w++) fir2dim();

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        fir2dim();
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

    /* Checksum: sum of center pixel column */
    int32_t chk = 0;
    for (int r = 0; r < IMG_ROWS; r++) chk += s_dst[r][IMG_COLS/2];
    printf("Result checksum (col %d sum): %d\n\n", IMG_COLS/2, chk);

    printf("%-24s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printf("%-24s %10llu %10llu %10llu %10llu\n",
           "FIR2DIM [QNX]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    free(exec_ns);
    return 0;
}
