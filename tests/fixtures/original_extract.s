// Original A64 EXTR and ROR (immediate alias) test fixture.
.text
.global _start
_start:
    extr w0, w1, w2, #0
    extr w3, w4, w5, #13
    extr w6, w7, w8, #31
    extr x9, x10, x11, #0
    extr x12, x13, x14, #7
    extr x15, x16, x17, #63
    ror w18, w19, #5
    ror x20, x21, #17
    ret
