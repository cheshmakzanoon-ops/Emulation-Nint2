// Original, redistributable AArch64 assembly test.
 // Clang 17: clang --target=aarch64-none-elf -c aarch64_smoke.s -o aarch64_smoke.o
.text
.global _start
_start:
    movz w0, #7
    add w0, w0, #35
    brk #0

 // The following independent sample verifies branches, call/return, memory, flags.
sample:
    movz x1, #0x1234
    movk x1, #0xabcd, lsl #16
    add x2, x1, #1
    subs x3, x2, x2
    b.eq after
    movz x0, #99
after:
    orr x4, xzr, x1
    eor x5, x4, x1
    cbz x5, next
    movz x0, #99
next:
    str x2, [sp, #8]
    ldr x6, [sp, #8]
    bl function
    brk #0
function:
    ret
