.text
.global _start
_start:
ldxrb w0,[x1]
ldaxrb w2,[x3]
stxrb w4,w5,[x6]
stlxrb w7,w8,[x9]
ldxrh w10,[x11]
ldaxrh w12,[x13]
stxrh w14,w15,[x16]
stlxrh w17,w18,[x19]
ldxr w20,[x21]
ldaxr w22,[x23]
stxr w24,w25,[x26]
stlxr w27,w28,[x29]
ldxr x0,[x1]
ldaxr x2,[x3]
stxr w4,x5,[x6]
stlxr w7,x8,[x9]
clrex
clrex #0
dmb ish
dsb sy
isb
ret
