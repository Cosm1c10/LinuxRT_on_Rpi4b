/*
 * bench_matrix1_qnx.c  —  Matrix Multiplication RT Benchmark  (QNX Neutrino target)
 * ===================================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration execution time and jitter for a square
 *           floating-point matrix multiply  (N x N  *  N x N).
 *           N = 128  →  ~2 million FP multiply-add operations per iter.
 *
 * Build (on host, cross-compile for RPi 4):
 *   qcc -V gcc_ntoaarch64le -D_QNX_SOURCE -O2 -lm \
 *       -o bench_matrix1_qnx tests/bench_matrix1_qnx.c
 *
 * Deploy & Run:
 *   scp bench_matrix1_qnx qnxuser@<RPI_IP>:/tmp/
 *   ssh qnxuser@<RPI_IP> "/tmp/bench_matrix1_qnx"
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS  1000   /* number of timed matrix multiply runs */
#define MATRIX_N    128    /* matrix dimension  (N x N)             */

/* ------------------------------------------------------------------ */
/*  Matrix storage  (statically allocated)                            */
/* ------------------------------------------------------------------ */
static float A[MATRIX_N][MATRIX_N];
static float B[MATRIX_N][MATRIX_N];
static float C[MATRIX_N][MATRIX_N];

/* ------------------------------------------------------------------ */
/*  Timing helper                                                      */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Matrix multiply: C = A * B  (naive triple loop)                   */
/* ------------------------------------------------------------------ */
static void matmul(void) {
    for (int i = 0; i < MATRIX_N; i++) {
        for (int j = 0; j < MATRIX_N; j++) {
            float s = 0.0f;
            for (int k = 0; k < MATRIX_N; k++)
                s += A[i][k] * B[k][j];
            C[i][j] = s;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printf("=== RT-Bench: MATRIX1  [QNX] ===\n");
    printf("Iterations  : %d\n", ITERATIONS);
    printf("Matrix size : %dx%d float32\n\n", MATRIX_N, MATRIX_N);

    /* Initialise matrices with deterministic values */
    for (int i = 0; i < MATRIX_N; i++)
        for (int j = 0; j < MATRIX_N; j++) {
            A[i][j] = (float)(i + j + 1) / (float)MATRIX_N;
            B[i][j] = (float)(i - j + MATRIX_N) / (float)MATRIX_N;
        }

    uint64_t *exec_ns = (uint64_t *)malloc(ITERATIONS * sizeof(uint64_t));
    if (!exec_ns) { fprintf(stderr, "malloc failed\n"); return 1; }

    /* Warm-up (not timed) */
    for (int w = 0; w < 3; w++) matmul();

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        matmul();
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

    /* Checksum to prevent dead-code elimination */
    float chk = 0.0f;
    for (int i = 0; i < MATRIX_N; i++) chk += C[i][i];
    printf("Result checksum (diagonal sum): %.4f\n\n", chk);

    printf("%-22s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printf("%-22s %10llu %10llu %10llu %10llu\n",
           "MATRIX1 [QNX]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    free(exec_ns);
    return 0;
}
