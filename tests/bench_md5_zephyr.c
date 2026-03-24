/*
 * bench_md5_zephyr.c  —  MD5 RT Benchmark  (Zephyr RTOS target)
 * ==============================================================
 * Reference: M. Nicolella, S. Roozkhosh, D. Hoornaert, A. Bastoni,
 *            R. Mancuso — "RT-bench: An extensible benchmark framework
 *            for the analysis and management of real-time applications"
 *            RTNS 2022.  gitlab.com/rt-bench/rt-bench
 *
 * Measures: Per-iteration MD5 execution time and jitter.
 *           Each iteration hashes a 64 KB buffer once.
 *
 * Build (west, target rpi_4b):
 *   Add this file to CMakeLists.txt sources and run:
 *   west build -b rpi_4b .
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Benchmark parameters                                               */
/* ------------------------------------------------------------------ */
#define ITERATIONS  1000
#define DATA_SIZE   (64 * 1024)

/* ------------------------------------------------------------------ */
/*  MD5 implementation (RFC 1321)                                      */
/* ------------------------------------------------------------------ */
#define MD5_ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))

static const uint32_t MD5_T[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static const uint8_t MD5_S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buf[64];
} MD5_CTX;

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],x[16];
    for (int i=0;i<16;i++) x[i]=((uint32_t)block[i*4])|(((uint32_t)block[i*4+1])<<8)|(((uint32_t)block[i*4+2])<<16)|(((uint32_t)block[i*4+3])<<24);
    for (int i=0;i<64;i++){
        uint32_t f,g;
        if(i<16){f=(b&c)|(~b&d);g=i;}
        else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}
        else if(i<48){f=b^c^d;g=(3*i+5)%16;}
        else{f=c^(b|~d);g=(7*i)%16;}
        f+=a+MD5_T[i]+x[g];
        a=d;d=c;c=b;b+=MD5_ROTL(f,MD5_S[i]);
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
}

static void md5_init(MD5_CTX *ctx) {
    ctx->state[0]=0x67452301;ctx->state[1]=0xefcdab89;
    ctx->state[2]=0x98badcfe;ctx->state[3]=0x10325476;
    ctx->count[0]=ctx->count[1]=0;
}

static void md5_update(MD5_CTX *ctx, const uint8_t *data, size_t len) {
    uint32_t idx=(ctx->count[0]>>3)&0x3F;
    ctx->count[0]+=(uint32_t)(len<<3);
    if(ctx->count[0]<(uint32_t)(len<<3)) ctx->count[1]++;
    ctx->count[1]+=(uint32_t)(len>>29);
    uint32_t part=64-idx;
    size_t i=0;
    if(len>=part){ memcpy(&ctx->buf[idx],data,part); md5_transform(ctx->state,ctx->buf); for(i=part;i+63<len;i+=64) md5_transform(ctx->state,data+i); idx=0; }
    memcpy(&ctx->buf[idx],&data[i],len-i);
}

static void md5_final(MD5_CTX *ctx, uint8_t digest[16]) {
    static const uint8_t pad[64]={0x80};
    uint8_t bits[8];
    for(int i=0;i<4;i++){bits[i]=(uint8_t)(ctx->count[0]>>(i*8));bits[i+4]=(uint8_t)(ctx->count[1]>>(i*8));}
    uint32_t idx=(ctx->count[0]>>3)&0x3f;
    uint32_t padlen=(idx<56)?56-idx:120-idx;
    md5_update(ctx,pad,padlen);
    md5_update(ctx,bits,8);
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) digest[i*4+j]=(uint8_t)(ctx->state[i]>>(j*8));
}

/* ------------------------------------------------------------------ */
/*  Timing helper  (Zephyr hardware cycle counter)                    */
/* ------------------------------------------------------------------ */
static inline uint64_t now_ns(void) {
    return k_cyc_to_ns_near64(k_cycle_get_64());
}

/* ------------------------------------------------------------------ */
/*  Static buffers                                                     */
/* ------------------------------------------------------------------ */
static uint8_t  s_input[DATA_SIZE];
static uint64_t s_exec_ns[ITERATIONS];

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    printk("=== RT-Bench: MD5  [Zephyr] ===\n");
    printk("Iterations : %d\n", ITERATIONS);
    printk("Input size : %d bytes per iteration\n\n", DATA_SIZE);

    for (int i = 0; i < DATA_SIZE; i++) s_input[i] = (uint8_t)(i & 0xFF);

    uint8_t digest[16];

    /* Warm-up */
    MD5_CTX ctx;
    for (int i = 0; i < 5; i++) {
        md5_init(&ctx);
        md5_update(&ctx, s_input, DATA_SIZE);
        md5_final(&ctx, digest);
    }

    /* Timed loop */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t t0 = now_ns();
        md5_init(&ctx);
        md5_update(&ctx, s_input, DATA_SIZE);
        md5_final(&ctx, digest);
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

    printk("MD5 digest (sample): ");
    for (int i = 0; i < 16; i++) printk("%02x", digest[i]);
    printk("\n\n");

    printk("%-24s %10s %10s %10s %10s\n",
           "Benchmark","Min(us)","Max(us)","Avg(us)","Jitter(us)");
    printk("%-24s %10llu %10llu %10llu %10llu\n",
           "MD5 [Zephyr]",
           (unsigned long long)(min_ns/1000),
           (unsigned long long)(max_ns/1000),
           (unsigned long long)(avg_ns/1000),
           (unsigned long long)(jitter/1000));

    return 0;
}
