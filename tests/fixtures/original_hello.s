// Original AArch64 guest program for the *synthetic* nint2 SVC ABI.
// x8=1: Write(fd=x0, buffer=x1, length=x2); x8=2: Exit(code=x0).
.text
.global _start
_start:
    movz x8, #1
    movz x0, #1
    adr  x1, message
    movz x2, #20
    svc #0
    movz x8, #2
    movz x0, #0
    svc #0
message:
    .ascii "Hello from AArch64!\n"
