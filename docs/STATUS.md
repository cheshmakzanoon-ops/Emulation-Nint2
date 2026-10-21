# Implementation status — 2026-10-21

**Stage: M0 early bootstrap. Not yet a functioning Nintendo Switch 2 emulator.**

## Present in main

- [x] 61-phase plan and staged hardware reverse-engineering methodology.
- [x] C++20 / CMake nint2_core static library, native nint2 CLI and CTest.
- [x] Page-mapped GuestMemory proof-of-concept: 4 KiB pages, map/unmap/protect, read/write single byte, checked ranges, W^X rejection.
- [x] Original unit test for mapping, invalid ranges, permission transitions, overlapping maps and unmaps.
- [x] Native CLI smoke command to validate basic host code.
- [x] GitHub Actions workflow for Linux and Windows builds and CTest.

**Local test evidence:** Equivalent foundation sources configured and built with CMake 3.31.6/GCC 14.2.0 on Linux, and CTest passed **2/2** on 2026-10-21. Windows CI results are not yet established by this note. GitHub Actions runs should be checked independently.

## Not implemented

- [ ] ARM64 interpreter, dynamic translator/JIT or execution of original guest code.
- [ ] ELF64 guest loader, executable module linking, guest virtual system clock.
- [ ] True sparse/lazy 12 GB-capable guest virtual memory, page aliasing, MMU semantics or fault delivery.
- [ ] Horizon kernel, SVC, IPC or native Switch 2 services.
- [ ] Native T239 GPU command processing, shader ISA, GPU VA or Vulkan backend.
- [ ] Input/physical Joy-Con 2, audio, save data, GUI or network stub layer.
- [ ] Hardware observation measurements on a Switch 2.
- [ ] Firmware/game support or compatibility evidence.

## Immediate ordered implementation backlog

1. Replace page-eager M0 memory backend with scalable sparse page metadata and lazy backing, guarded size arithmetic, shared mappings and tests.
2. Build an AArch64 interpreter executing original 7+35=42 machine-code fixture with breakpoints.
3. Introduce original ELF64/AArch64 cross-built test artifact and parser, fuzz malformed files.
4. Build CPU behavior/extension matrix, audit and pin a Dynarmic fork.
5. Implement kernel handles, minimal SVC and deterministic IPC fixtures.
6. Parallel research: Vulkan headless triangle, Switch 2 T239 GPU fact/evidence cards, original shader IR.

Update this status *after* passing any further CI or actual-device tests; never silently change an unchecked item to complete.
