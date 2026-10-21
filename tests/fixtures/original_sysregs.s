.text
.global _start
_start:
mrs x0, nzcv
msr nzcv, x1
mrs x2, tpidr_el0
msr tpidr_el0, x3
mrs x4, tpidrro_el0
mrs x5, fpcr
msr fpcr, x6
mrs x7, fpsr
msr fpsr, x8
mrs x9, cntfrq_el0
mrs x10, cntvct_el0
ret
