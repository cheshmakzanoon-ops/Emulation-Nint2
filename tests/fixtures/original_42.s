// Original redistributable AArch64 assembly fixture.
// Clang AArch64 target and LLD can link as a static ET_EXEC image.
.text
.global _start
_start:
    movz w0, #7
    add w0, w0, #35
    brk #0
