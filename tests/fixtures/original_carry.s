// Independently authored AArch64 carry-chain instruction fixture.
.text
.global _start
_start:
    adc x0, x1, x2
    adcs w3, w4, w5
    sbc x6, x7, x8
    sbcs w9, w10, w11
    ngc x12, x13
    ngcs w14, w15
    adc w16, wzr, w17
    adcs x18, x19, x20
    sbcs x21, x22, x23
    ret
