// Original register-31 and rotated logical-operand regression fixture.
.text
.global original_sp_logical
original_sp_logical:
    adds x0, sp, #16
    subs w1, wsp, #1
    cmp sp, #32
    cmn wsp, #1
    and sp, x2, #0xfffffffffffffff0
    orr wsp, w3, #0xff
    eor sp, x4, #0xff00ff00ff00ff00
    ands xzr, x5, #0xff
    orr x6, xzr, x7, ror #0
    and w8, w9, w10, ror #31
    eon x11, x12, x13, ror #17
    bics w14, w15, w16, ror #8
    and sp, xzr, #0xfffffffffffffff0
    orr xzr, xzr, x7, ror #0
    ret
    .inst 0x8bc20020 // unallocated ADD shifted-register ROR
    .inst 0x2ac28020 // unallocated W-form logical shift of 32
