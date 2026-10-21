// Original AArch64 instruction fixture assembled with Clang 17, not Nintendo code.
.text
.global _start
_start:
    and w0, w1, #0xff
    orr w2, w3, #0x80000001
    eor x4, x5, #0xff00ff00ff00ff00
    ands x6, x7, #0xfff
    bic x8, x9, x10
    orn w11, w12, w13, lsr #3
    eon x14, x15, x16, asr #2
    bics w17, w18, w19
    and w20, w21, #0x000f000f
    orr x22, x23, #0x8000000080000000
    ands w24, w25, #1
    ands x26, x27, #0x8000000000000000
    ret
