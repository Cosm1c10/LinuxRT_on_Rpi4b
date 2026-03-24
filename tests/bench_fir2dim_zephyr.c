/*
 * bench_fir2dim_zephyr.c  —  2-D FIR Filter RT Benchmark  (Zephyr RTOS target)
 * ==============================================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration 2-D FIR convolution execution time and jitter.
 *           Each iteration convolves a 256x256 int16 image with a 5x5
 *           integer kernel (no floating point required).
 *
 * Build (west, target rpi_4b):
 *   Add this file to CMakeLists.txt sources and run:
 *   west build -b rpi_4b .
 *
 * Note: The two large image arrays (~128 KB combined) are placed in .bss.
 *       Ensure CONFIG_SRAM_SIZE covers this — rpi_4b has ample SRAM.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>

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
#define KERNEL_SUM_LOG2  8

/* ------------------------------------------------------------------ */
/*  Static image buffers and timing array                             */
/* ------------------------------------------------------------------ */
static int16_t  s_src[IMG_ROWS][IMG_COLS];
static int16_t  s_dst[IMG_ROWS][IMG_COLS];
static uint64_t s_exec_ns[ITERATIONS];

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
/*  Timing helper  (Zephyr hardware cycle counter)                    */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    return k_cyc_to_ns_near64(k_cycle_get_64());
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printk("=== RT-Bench: FIR2DIM  [Zephyr] ===\n");
    printk("Iterations  : %d\n", ITERATIONS);
    printk("Image size  : %dx%d int16\n", IMG_ROWS, IMG_COLS);
    printk("Kernel size : %dx%d\n\n", KERNEL_SIZE, KERNEL_SIZE);

    /* Initialise source image with ramp data */
    for (int r = 0; r < IMG_ROWS; r++)
        for (int c = 0; c < IMG_COLS; c++)
            s_src[r][c] = (int16_t)((r * IMG_COLS + c) & 0xFF);

    /* Warm-up */
    for (int w = 0; w < 3; w++) fir2dim();

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        fir2dim();
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

    /* Checksum: sum of center pixel column */
    int32_t chk = 0;
    for (int r = 0; r < IMG_ROWS; r++) chk += s_dst[r][IMG_COLS/2];
    printk("Result checksum (col %d sum): %d\n\n", IMG_COLS/2, chk);

    printk("%-26s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printk("%-26s %10llu %10llu %10llu %10llu\n",
           "FIR2DIM [Zephyr]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    return 0;
}
