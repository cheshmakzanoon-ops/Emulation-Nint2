// Original AArch64 assembly, independently assembled with Clang 17.
.text
.global _start
_start:
    udiv w0, w1, w2
    sdiv w3, w4, w5
    udiv x6, x7, x8
    sdiv x9, x10, x11
    lslv w12, w13, w14
    lsrv x15, x16, x17
    asrv w18, w19, w20
    asrv x21, x22, x23
    rorv w24, w25, w26
    rorv x27, x28, x29
    ret
