// Original AArch64 freestanding C application for the documented synthetic
// SVC #0 test protocol; not a Linux, Horizon or Nintendo program.
typedef unsigned long long u64;
typedef signed long long s64;
typedef unsigned int u32;
typedef signed int s32;

// Volatile source values prevent Clang from constant-folding integer division
// and variable shifts. The generated ELF must execute actual A64 instructions.
static volatile u64 input = 0x123456789abcdef0ULL;
static volatile u64 divisor = 13;
static volatile s64 signed_input = -987654321LL;
static volatile s64 signed_divisor = 17;
static const char good[] = "AArch64 C divide verified!\n";

static inline u64 service(u64 nr, u64 a, u64 b, u64 c) {
    register u64 r0 __asm__("x0") = a;
    register u64 r1 __asm__("x1") = b;
    register u64 r2 __asm__("x2") = c;
    register u64 r8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r8) : "memory", "cc");
    return r0;
}

void _start(void) {
    const u64 a = input;
    const u64 d = divisor;
    const s64 b = signed_input;
    const s64 s = signed_divisor;
    const u64 q = a / d;
    const s64 sq = b / s;
    const u32 narrow = (u32)a;
    const u32 v = narrow << (d & 31);
    const s32 arithmetic = ((s32)b) >> (d & 31);
    const u64 rotated = (a >> (d & 63)) | (a << ((64 - d) & 63));
    const int valid = q == 0x1667cce33499afeULL && sq == -58097313LL &&
                      v == 0x9bde0000U && arithmetic == -120564 &&
                      rotated == 0xf78091a2b3c4d5e6ULL;
    if (valid) (void)service(1, 1, (u64)good, sizeof(good) - 1);
    (void)service(2, valid ? 0 : 97, 0, 0);
    for (;;) { }
}
