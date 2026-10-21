// Original authored test-only AArch64 program (not Nintendo code).
// x19 = process-local send queue handle; x20 unused.
.text
.global _start
_start:
    mov x8, #9
    mov x0, x19
    mov x1, #1
    mov x2, #1
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #5
    mov x0, x19
    adr x1, packet
    mov x2, #5
    svc #0
    cmp x0, #5
    b.ne bad
    mov x8, #2
    mov x0, #0
    svc #0
bad:
    mov x8, #2
    mov x0, #72
    svc #0
packet:
    .ascii "WAKE!"
