// Independently authored AArch64 instruction-encoding reference, not a Nintendo binary.
// clang --target=aarch64-none-elf -c original_integer_extensions.s -o /tmp/extra.o
.text
.globl _start
_start:
    stp x1, x2, [sp, #-16]!
    ldp x3, x4, [sp], #16
    stur x5, [x6, #-8]
    ldur w7, [x6, #-4]
    strb w8, [x9, x10]
    ldrb w11, [x9, x10]
    tbz w12, #3, target
    tbnz x13, #40, target
    ldr x14, value
    ldrsw x15, value
    csel x16, x17, x18, eq
    csinc w19, w20, w21, ne
    csinv w22, w23, w24, eq
    csneg x25, x26, x27, ne
    madd x0, x1, x2, x3
    msub w4, w5, w6, w7
    sxtw x8, w9
    ubfm x10, x11, #8, #23
    sbfm w12, w13, #4, #11
    ubfm x14, x15, #60, #7
    sbfm x16, x17, #60, #7
target:
    ret
.p2align 3
value:
    .word 0xffffffff
