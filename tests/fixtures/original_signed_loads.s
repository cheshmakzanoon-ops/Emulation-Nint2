// Independently authored AArch64 signed-load test. Assemble with:
// clang --target=aarch64-none-elf -c original_signed_loads.s -o original_signed_loads.o
.text
.global _start
_start:
    ldrsb w0, [x1, #0]
    ldrsb x2, [x1, #1]
    ldrsh w3, [x1, #2]
    ldrsh x4, [x1, #4]
    ldrsw x5, [x1, #8]
    ldursb w6, [x1, #-1]
    ldursh x7, [x1, #-2]
    ldursw x8, [x1, #-4]
    ldrsb w9, [x1, x10]
    ldrsh x11, [x1, w12, uxtw #1]
    ldrsw x13, [x1, x14, lsl #2]
    ldrsb w15, [x1], #1
    ldrsh x16, [x1, #-2]!
    brk #0
    str b0, [x1] // SIMD must remain unsupported, not decoded as scalar STRB.
