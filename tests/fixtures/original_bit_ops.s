// Original independently Clang-assembled AArch64 scalar data-processing fixture.
// clang --target=aarch64-none-elf -c original_bit_ops.s -o original_bit_ops.o
.text
.global _start
_start:
    rbit w0, w1
    rbit x2, x3
    rev16 w4, w5
    rev16 x6, x7
    rev32 x8, x9
    rev w10, w11
    rev x12, x13
    clz w14, w15
    clz x16, x17
    cls w18, w19
    cls x20, x21
    brk #0
