// Original independent AArch64 source for the nint2 synthetic asset ABI.
// The test launcher initializes x20 to a private writable guest buffer.
// x8=10 OpenAsset, 11 ReadAsset, 13 CloseAsset, 1 Write, 2 Exit.
.text
.global _start
_start:
    mov x8, #10
    adr x0, path
    mov x1, #13
    mov x2, #0
    svc #0
    cmp x0, #0
    b.lt bad
    mov x19, x0
    mov x8, #11
    mov x0, x19
    mov x1, x20
    mov x2, #19
    svc #0
    cmp x0, #19
    b.ne bad
    mov x21, x0
    mov x8, #1
    mov x0, #1
    mov x1, x20
    mov x2, x21
    svc #0
    cmp x0, #19
    b.ne bad
    mov x8, #13
    mov x0, x19
    svc #0
    cmp x0, #0
    b.ne bad
    mov x8, #2
    mov x0, #0
    svc #0
bad:
    mov x8, #2
    mov x0, #90
    svc #0
path:
    .ascii "/demo/message"
