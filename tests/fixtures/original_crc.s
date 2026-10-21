.text
.arch armv8.1-a+crc
.global _start
_start:
crc32b w0, w1, w2
crc32h w3, w4, w5
crc32w w6, w7, w8
crc32x w9, w10, x11
crc32cb w12, w13, w14
crc32ch w15, w16, w17
crc32cw w18, w19, w20
crc32cx w21, w22, x23
crc32b wzr, wzr, wzr
crc32cx wzr, wzr, xzr
ret
