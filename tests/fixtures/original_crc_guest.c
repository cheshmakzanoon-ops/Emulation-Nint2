// Original freestanding AArch64 compiler fixture. Not a Horizon/Switch ABI.
#include <stdint.h>

static const unsigned char digits[9] = {'1','2','3','4','5','6','7','8','9'};
static const char ok[] = "Compiled AArch64 CRC verified!\n";

static inline long service(long id, long a, long b, long c) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = id;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

__attribute__((noinline)) static uint32_t crc_bytes(int use_castagnoli) {
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 9; ++i) {
        if (use_castagnoli) crc = __builtin_arm_crc32cb(crc, digits[i]);
        else crc = __builtin_arm_crc32b(crc, digits[i]);
    }
    return ~crc;
}

void _start(void) {
    if (crc_bytes(0) != 0xcbf43926u) { service(2, 91, 0, 0); return; }
    if (crc_bytes(1) != 0xe3069283u) { service(2, 92, 0, 0); return; }

    // Test the wide X source variant on eight bytes plus a final CRC32B.
    // Each AArch64 CRC instruction consumes the integer source in LE order.
    const uint64_t first_eight = 0x3837363534333231ULL;
    uint32_t c32 = __builtin_arm_crc32d(0xffffffffu, first_eight);
    c32 = __builtin_arm_crc32b(c32, '9');
    if (~c32 != 0xcbf43926u) { service(2, 93, 0, 0); return; }

    uint32_t c32c = __builtin_arm_crc32cd(0xffffffffu, first_eight);
    c32c = __builtin_arm_crc32cb(c32c, '9');
    if (~c32c != 0xe3069283u) { service(2, 94, 0, 0); return; }

    const long written = service(1, 1, (long)ok, sizeof(ok)-1);
    service(2, written == (long)(sizeof(ok)-1) ? 0 : 95, 0, 0);
}
