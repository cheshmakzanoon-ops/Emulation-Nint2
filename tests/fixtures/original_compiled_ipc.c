// Independently authored freestanding C guest for the nint2 synthetic SVC ABI.
// NOT Nintendo Switch 2/Horizon firmware, services, titles or decrypted code.
// Build with:
// clang --target=aarch64-none-elf -O0 -ffreestanding -fno-builtin
//   -fno-stack-protector -fno-unwind-tables -nostdlib -fuse-ld=lld
//   -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096
//   original_compiled_ipc.c -o original_compiled_ipc.elf
#include <stdint.h>

static const int8_t changes[6] = {-3, 4, -1, 8, -5, 9};
static const int16_t multipliers[6] = {10, -2, 7, -4, 6, -1};
static const char message[] = "C guest IPC verified!\n";
static char received[sizeof(message)];

__attribute__((noinline)) static int32_t signed_sum(void) {
    int32_t sum = 0;
    for (unsigned i = 0; i < 6; ++i)
        sum += (int32_t)changes[i] * (int32_t)multipliers[i];
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
    if (signed_sum() != -116) {
        service(2, 91, 0, 0);
        return;
    }
    long handle = service(4, 2, 0, 0);
    if (handle <= 0) {
        service(2, 92, 0, 0);
        return;
    }
    const long size = sizeof(message) - 1;
    if (service(5, handle, (long)message, size) != size ||
        service(6, handle, (long)received, sizeof(received)) != size) {
        service(2, 93, 0, 0);
        return;
    }
    for (long i = 0; i < size; ++i) {
        if (received[i] != message[i]) {
            service(2, 94, 0, 0);
            return;
        }
    }
    if (service(1, 1, (long)received, size) != size || service(7, handle, 0, 0) != 0) {
        service(2, 95, 0, 0);
        return;
    }
    service(2, 0, 0, 0);
}
