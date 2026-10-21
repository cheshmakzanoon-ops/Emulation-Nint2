// Original AArch64 program for synthetic guest arena services 18..21.
// The host initializes x20 with a writable, private 24-byte query buffer.
// No Nintendo binaries, ABI assumptions or proprietary firmware are used.
.text
.global _start
_start:
    mov x8, #18
    mov x0, #4096
    mov x1, #3
    mov x2, #0
    svc #0
    cmp x0, #0
    b.lt bad
    mov x19, x0
    mov w1, #86
    strb w1, [x19]
    mov w1, #77
    strb w1, [x19, #1]
    mov w1, #33
    strb w1, [x19, #2]
    mov w1, #10
    strb w1, [x19, #3]
    mov x8, #1
    mov x0, #1
    mov x1, x19
    mov x2, #4
    svc #0
    cmp x0, #4
    b.ne bad
    mov x8, #20
    mov x0, x19
    mov x1, #5
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #21
    mov x0, x19
    mov x1, x20
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #19
    mov x0, x19
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #2
    mov x0, #0
    svc #0
bad:
    mov x8, #2
    mov x0, #77
    svc #0
