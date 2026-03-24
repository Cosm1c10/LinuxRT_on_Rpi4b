/*
 * bench_matrix1_zephyr.c  —  Matrix Multiplication RT Benchmark  (Zephyr RTOS target)
 * =====================================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration execution time and jitter for a square
 *           floating-point matrix multiply  (N x N  *  N x N).
 *           N = 128  →  ~2 million FP multiply-add operations per iter.
 *
 * Build (west, target rpi_4b):
 *   Add this file to CMakeLists.txt sources and run:
 *   west build -b rpi_4b .
 *
 * prj.conf requirements:
 *   CONFIG_FPU=y
 *   CONFIG_FPU_SHARING=y
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS  1000   /* number of timed matrix multiply runs */
#define MATRIX_N    128    /* matrix dimension  (N x N)             */

/* ------------------------------------------------------------------ */
/*  Matrix storage  (static — no heap needed)                         */
/* ------------------------------------------------------------------ */
static float s_A[MATRIX_N][MATRIX_N];
static float s_B[MATRIX_N][MATRIX_N];
static float s_C[MATRIX_N][MATRIX_N];

static uint64_t s_exec_ns[ITERATIONS];

/* ------------------------------------------------------------------ */
/*  Timing helper  (Zephyr hardware cycle counter)                    */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    return k_cyc_to_ns_near64(k_cycle_get_64());
}

/* ------------------------------------------------------------------ */
/*  Matrix multiply: C = A * B  (naive triple loop)                   */
/* ------------------------------------------------------------------ */
static void matmul(void) {
    for (int i = 0; i < MATRIX_N; i++) {
        for (int j = 0; j < MATRIX_N; j++) {
            float s = 0.0f;
            for (int k = 0; k < MATRIX_N; k++)
                s += s_A[i][k] * s_B[k][j];
            s_C[i][j] = s;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printk("=== RT-Bench: MATRIX1  [Zephyr] ===\n");
    printk("Iterations  : %d\n", ITERATIONS);
    printk("Matrix size : %dx%d float32\n\n", MATRIX_N, MATRIX_N);

    /* Initialise matrices with deterministic values */
    for (int i = 0; i < MATRIX_N; i++)
        for (int j = 0; j < MATRIX_N; j++) {
            s_A[i][j] = (float)(i + j + 1) / (float)MATRIX_N;
            s_B[i][j] = (float)(i - j + MATRIX_N) / (float)MATRIX_N;
        }

    /* Warm-up (not timed) */
    for (int w = 0; w < 3; w++) matmul();

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        matmul();
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

    /* Checksum to prevent dead-code elimination */
    float chk = 0.0f;
    for (int i = 0; i < MATRIX_N; i++) chk += s_C[i][i];
    /* Print as integer*10000 since printk has no %f on all configs */
    printk("Result checksum (diag sum x10000): %d\n\n", (int)(chk * 10000.0f));

    printk("%-26s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printk("%-26s %10llu %10llu %10llu %10llu\n",
           "MATRIX1 [Zephyr]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    return 0;
}
