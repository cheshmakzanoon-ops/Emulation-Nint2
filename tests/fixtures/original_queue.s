// Independently authored test program. SVC IDs are nint2 synthetic ABI, NOT Horizon.
.text
.global _start
_start:
    movz x8, #4
    movz x0, #2
    svc #0
    orr x19, xzr, x0
    movz x8, #5
    orr x0, xzr, x19
    adr x1, message
    movz x2, #18
    svc #0
    movz x8, #6
    orr x0, xzr, x19
    adr x1, output
    movz x2, #18
    svc #0
    movz x8, #1
    movz x0, #1
    adr x1, output
    movz x2, #18
    svc #0
    movz x8, #7
    orr x0, xzr, x19
    svc #0
    movz x8, #2
    movz x0, #0
    svc #0
message:
    .ascii "Hello via kernel!\n"
.bss
.p2align 12
output:
    .skip 64
