// Original authored test-only AArch64 program (not Nintendo code).
// x19 = process-local receive queue handle; x20 = writable 5-byte buffer.
.text
.global _start
_start:
    mov x8, #9
    mov x0, x19
    mov x1, #2
    mov x2, #1
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #6
    mov x0, x19
    mov x1, x20
    mov x2, #5
    svc #0
    cmp x0, #5
    b.ne bad
    mov x8, #1
    mov x0, #1
    mov x1, x20
    mov x2, #5
    svc #0
    mov x8, #2
    mov x0, #0
    svc #0
bad:
    mov x8, #2
    mov x0, #71
    svc #0
