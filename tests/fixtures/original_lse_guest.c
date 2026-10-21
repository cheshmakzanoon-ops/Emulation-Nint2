// Original, freestanding Clang ARMv8.1 LSE compiler fixture.
// Synthetic SVC numbers are project-specific; NOT a Nintendo/Horizon ABI.
#include <stdint.h>

static _Atomic uint64_t counter = 3;
static _Atomic uint32_t mask = 0x00ff00ffu;
static const char success[] = "Compiled ARM64 LSE atomics verified!\n";

static inline long service(long id, long arg0, long arg1, long arg2) {
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;
    register long x8 __asm__("x8") = id;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

void _start(void) {
    uint64_t old = __c11_atomic_fetch_add(&counter, 7, __ATOMIC_ACQ_REL);
    if (old != 3) { service(2, 91, 0, 0); return; }

    uint64_t expected = 10;
    if (!__c11_atomic_compare_exchange_strong(
            &counter, &expected, 42, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        service(2, 92, 0, 0);
        return;
    }

    expected = 41;
    if (__c11_atomic_compare_exchange_strong(
            &counter, &expected, 99, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED) ||
        expected != 42) {
        service(2, 93, 0, 0);
        return;
    }

    const uint32_t before =
        __c11_atomic_fetch_xor(&mask, 0xff00ff00u, __ATOMIC_ACQ_REL);
    if (before != 0x00ff00ffu ||
        __c11_atomic_load(&mask, __ATOMIC_ACQUIRE) != 0xffffffffu ||
        __c11_atomic_load(&counter, __ATOMIC_ACQUIRE) != 42) {
        service(2, 94, 0, 0);
        return;
    }

    const long written = service(1, 1, (long)success, sizeof(success) - 1);
    service(2, written == (long)(sizeof(success) - 1) ? 0 : 95, 0, 0);
}
