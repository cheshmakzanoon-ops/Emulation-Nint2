.text
.arch armv8.1-a+lse
.global _start
_start:
ldxp w0, w1, [x2]
ldaxp w3, w4, [x5]
stxp w6, w7, w8, [x9]
stlxp w10, w11, w12, [x13]
ldxp x0, x1, [x2]
ldaxp x3, x4, [x5]
stxp w6, x7, x8, [x9]
stlxp w10, x11, x12, [x13]
casp w0, w1, w2, w3, [x4]
caspa w6, w7, w8, w9, [x10]
caspl w12, w13, w14, w15, [x16]
caspal w20, w21, w22, w23, [x24]
casp x0, x1, x2, x3, [x4]
caspa x6, x7, x8, x9, [x10]
caspl x12, x13, x14, x15, [x16]
caspal x20, x21, x22, x23, [x24]
ret
