# Implementation status — 2026-10-21

**Stage: M0 foundation + partial phase 17. Not yet a functioning Nintendo Switch 2 emulator.**

## Present in main

- [x] 61-phase plan, provenance rules, C++20/CMake/CTest CLI and Windows/Linux CI.
- [x] Page-granular guest address mapping, guards and permission transitions, W^X rejection.
- [x] **Phase 17 (partial):** lazy physical page buffers, coherent aliases (including before first write), little-endian 8/16/32/64-bit cross-page operations, execute-permission-checked 32-bit instruction fetch.
- [x] Tests for mapping/overlap/permissions, writes after aliases, forbidden write/execute aliases, cross-page byte order, and virtual-address overflow.
- [x] Native CLI memory smoke command.

**Local validation for this milestone:** 2026-10-21 Linux x64, CMake 3.31.6, GCC 14.2.0. Successfully ran configure, build and CTest: **2/2 passed**. Windows CI not yet independently checked for this commit.

## Honest limitations

- Page *data* allocation is lazy but page *metadata* remains one entry per mapped 4 KiB page. Not a completed 12 GB-scale VM.
- No host page-fault delivery, deferred module demand paging, protected JIT alias transitions or guest MMU translation.
- No AArch64 interpreter/JIT, ELF loader, kernel, HLE, native GPU, hardware research measurements or retail software compatibility yet.
- Project outbound license selection remains unresolved.

## Immediate next steps

1. Implement independently authored AArch64 reference interpreter (MOVZ, ADD, branches, BRK), original machine-code fixture, and deterministic failure reporting.
2. Extend ISA and instruction tests; implement bounded authored ELF64 loader.
3. Add kernel and service stubs only after CPU/loader fixtures work.
4. Validate Linux/Windows CI and add sanitizers/fuzz targets.

No claims of Switch 2 software execution are made.
