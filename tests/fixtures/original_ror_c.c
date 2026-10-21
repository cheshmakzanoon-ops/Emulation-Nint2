// Independently authored optimized AArch64 C guest for EXTR/ROR immediate.
// Only the project's documented synthetic SVC interface is used.
typedef unsigned long long u64;
static volatile u64 input = 0x123456789abcdef0ULL;
static volatile u64 result_value;
static const char text[] = "Optimized ARM64 rotate!\n";

static inline u64 svc(u64 number, u64 a, u64 b, u64 c) {
    register u64 x0 __asm__("x0") = a;
    register u64 x1 __asm__("x1") = b;
    register u64 x2 __asm__("x2") = c;
    register u64 x8 __asm__("x8") = number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8)
                     : "memory", "cc");
    return x0;
}
void _start(void) {
    const u64 value = input;
    const u64 rotated = (value >> 17) | (value << 47);
    // Explicitly volatile: Clang is otherwise free to algebraically rewrite
    // a comparison of ROR(value, 17) against a constant to compare value.
    // This forces execution of the actual rotated result.
    result_value = rotated;
    const int valid = result_value == 0x6f78091a2b3c4d5eULL;
    if (valid) (void)svc(1, 1, (u64)text, sizeof(text) - 1);
    (void)svc(2, valid ? 0 : 47, 0, 0);
    for (;;) { }
}
