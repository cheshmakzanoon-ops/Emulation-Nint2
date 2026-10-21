// Independently authored AArch64 C program. This uses a synthetic test-only
// syscall convention. It is not a Nintendo/Horizon program or firmware.
#include <stdint.h>

static const char greeting[] = "Compiled C on AArch64!\n";

__attribute__((noinline)) static uint32_t checksum(const char *text, uint32_t count) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; ++i)
        sum += ((uint32_t)(uint8_t)text[i]) * (i + 1);
    return sum;
}

static long service(long call, long a, long b, long c) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = call;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

void _start(void) {
    const uint32_t result = checksum(greeting, sizeof(greeting) - 1);
    if (result != 19103u) {
        service(2, 99, 0, 0);
        return;
    }
    const long written = service(1, 1, (long)greeting, sizeof(greeting) - 1);
    service(2, written == (long)(sizeof(greeting) - 1) ? 0 : 1, 0, 0);
}
