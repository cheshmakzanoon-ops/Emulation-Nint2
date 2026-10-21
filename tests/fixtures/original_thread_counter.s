.text
.global _start
_start:
// X0 -> private original guest counter; X1 -> increments per guest thread.
// The retry loop verifies LDAXR/STLXR and shared physical-frame monitors.
.Lretry:
    ldaxr w2, [x0]
    add w2, w2, #1
    stlxr w3, w2, [x0]
    cbnz w3, .Lretry
    subs x1, x1, #1
    b.ne .Lretry
    brk #0
