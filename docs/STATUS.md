# Implementation status — 2026-10-21

**Stage: M0 foundation + partial phases 17–18 (reference AArch64 execution). Not a functional Nintendo Switch 2 emulator.**

## Present in main

- [x] 61-phase plan, provenance rules, C++20/CMake/CTest CLI and Windows/Linux CI.
- [x] Phase 17 partial: page metadata with lazy page data, coherent aliases, guarded permissions, little-endian typed cross-page reads/writes, executable instruction fetch.
- [x] Phase 18 partial: independently written AArch64 interpreter, register/SP/PC/NZCV state; MOVZ/MOVK/MOVN, ADD/SUB immediate and shifted-register, basic logical ops, conditional/unconditional branches, call/return, LDR/STR unsigned offset, BRK/SVC trap and explicit invalid instruction/data/execute faults.
- [x] **Breakthrough:** original machine-code fixture 'movz w0,#7; add w0,w0,#35; brk #0' executes and produces x0=42. CLI '--cpu-smoke' works without console images.
- [x] Authored assembler fixture source, deterministic CPU tests for branches, load/store, memory faults, flags, register wrapping and 512 pseudorandom addition vectors.

**Local validation:** 2026-10-21 Linux x64: CMake 3.31.6, GCC 14.2.0, Clang 17. Configure/build/CTest **4/4 passed** for GCC and Clang; GCC UBSan build **4/4 passed**. Windows CI not yet independently verified.

## Honest limitations

- Phase 17 page *metadata* remains eager per mapped 4 KiB page; physical page data is lazy. No full 12 GB process address-space scale guarantee.
- Interpreter only handles an intentionally narrow ISA subset. No FP/NEON, atomics, PAC, sysregs, exceptions beyond explicit stops, JIT, Linux/Horizon syscalls, or scheduler.
- No ELF loader yet; smoke command copies original encoded words into guest memory.
- No host MMU, true service ABI, native GPU support, Switch 2 hardware measurements, firmware or commercial-game compatibility.
- Outbound project license unresolved.

## Next dependency-ordered milestones

1. Validate Linux and Windows GitHub Actions checks.
2. Add independently authored ELF64/AArch64 PT_LOAD loader and run its '7+35' guest program.
3. Expand interpreter test corpus and fault classification before introducing JIT.
4. Then kernel scheduling, handles, independent SVC and IPC fixtures.
