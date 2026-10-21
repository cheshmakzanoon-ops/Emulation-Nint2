// An original independent AArch64 ELF program using the nint2-only
// synthetic heap (SVC 18/19) and immutable assets (SVC 10/11/13).
// No Nintendo firmware, copyrighted assets, or native Horizon ABI.
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
    mov x8, #10
    adr x0, path
    mov x1, #13
    mov x2, #0
    svc #0
    cmp x0, #0
    b.lt bad
    mov x21, x0
    mov x8, #11
    mov x0, x21
    mov x1, x19
    mov x2, #19
    svc #0
    cmp x0, #19
    b.ne bad
    mov x8, #1
    mov x0, #1
    mov x1, x19
    mov x2, #19
    svc #0
    cmp x0, #19
    b.ne bad
    mov x8, #13
    mov x0, x21
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
    mov x0, #91
    svc #0
path:
    .ascii "/demo/message"
