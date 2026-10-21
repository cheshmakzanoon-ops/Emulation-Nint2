# Implementation status — 2026-10-21

**Stage: M0 + partial phases 17, 18, 22. A native original AArch64 ELF test program runs. Still not a Nintendo Switch 2 emulator.**

## Implemented

- [x] 61-phase architecture, hardware research, provenance plan; C++20, CMake, CLI, CTest, Windows/Linux CI.
- [x] Partial phase 17: lazy guest page data, guarded page metadata, coherent aliases, W^X, 8/16/32/64-bit cross-page operations, executable instruction fetch.
- [x] Partial phase 18: original narrow AArch64 interpreter: integer MOV, ADD/SUB, logical/shift, branch/call/return, loads/stores, explicit BRK/SVC/undefined/fault stops.
- [x] Partial phase 22: independently authored ELF64 ET_EXEC little-endian AArch64 loader supporting static PT_LOAD files, BSS zeros, segment alignment, bounds and entry verification, page protection and rollback on failure.
- [x] Original 7+35 instruction bytes and compiler-linked ELF both produce x0=42 via the interpreter.
- [x] CLI --cpu-smoke and --run-elf path; bounded 1 GiB guest address space, 1-million-instruction run limit.
- [x] Original code tests: CPU branch/memory/fault/512 arithmetic vectors, ELF invalid headers/segments/256 deterministic mutations.

## Tests actually executed

2026-10-21 Linux x64, CMake 3.31.6:
- GCC 14.2.0: configure/build, CTest 5/5 passed.
- Clang 17: configure/build, CTest 5/5 passed.
- GCC UBSan CTest 5/5 passed.
- GCC ASan CTest 5/5 passed.
- Clang AArch64 cross-assembled and linked tests/fixtures/original_42.s into an ELF with LLD.
- Local CLI nint2 --run-elf original_42.elf produced: stop=1 (BRK), x0=42, steps=3, pc=0x10008.
- GitHub Actions Linux and Windows runs were still queued when checked. No remote CI result claimed.

## Missing or limited

- No full ARMv8 instruction set, JIT, Horizon kernel, real Horizon service ABI/SVC handler, renderer, audio, input, or executable containers for Nintendo software.
- ELF loader rejects dynamic linking, ET_DYN, relocations, overlapped LOAD pages, W+X segments, and unsupported cases.
- Lazy *data* buffers but one guest page metadata entry per mapped 4 KiB page.
- No native Switch 2 compatibility, games, firmware, keys or performance claims.
- Final project license undecided.

## Next milestones

1. Confirm Windows/Linux/sanitizer GitHub Actions results.
2. Expand original AArch64 instruction/ELF conformance corpus and fuzz harness.
3. Implement deterministic guest process, synthetic SVC dispatch and IPC under independently designed interfaces.
4. Validate additional Switch 2-specific interfaces only using lawful measured evidence.

## 2026-10-21 interpreter correctness increment

- Fixed ADD/ADDS W-register carry-flag calculation: compute unsigned carry after 32-bit result truncation.
- Implemented architecturally encoded ADR/ADRP signed 21-bit PC-relative address generation (including negative offset and page offset cases), verified with original Clang-assembled encodings.
- Tests actually run locally: CMake configure/build + CTest **5/5 GCC**, **5/5 Clang**, **5/5 GCC UBSan**. No new Windows/remote CI result is claimed.
- Next: synthetic guest-only SVC boundary and independently authored ELF64 program that writes output and exits; **this will not claim any actual Switch 2 Horizon ABI**.

## 2026-10-21 synthetic guest service and output milestone

**Verified end-to-end breakthrough:** original AArch64 ELF64 program generated from [tests/fixtures/original_hello.s](../tests/fixtures/original_hello.s) executes via the interpreter, addresses its own message with ADR, passes the guest pointer to SVC #0, writes `Hello from AArch64!\n`, and exits with status 0 after **8 guest instructions**.

- [x] `SyntheticSession` dispatches *original, explicitly non-Horizon* guest services for bounded Write, Exit and deterministic Ticks. Unknown calls and SVC immediates stop, not fake success.
- [x] Read-permission-checked guest buffers; no host-pointer passthrough. Up to 4096 bytes/call and 65536 captured bytes/session, invalid-pointer rollback, errors for invalid descriptors/ranges and output overflow.
- [x] New `nint2 --run-synthetic <original.elf>` CLI command; original `--run-elf` breakpoint mode remains functional.
- [x] Independent source assembly and original program bytes integrated in a generated ELF test fixture; CTest auto-generates a binary in the **build** folder (not repository) before an end-to-end CLI test.
- [x] Local Linux x64 CMake configure/build and CTest: **8/8 GCC**, **8/8 Clang**, **8/8 GCC UBSan**, **8/8 GCC ASan+UBSan**. Cross-linked Clang+LLD original_hello.s independently tested via CLI with the same output/exit. CTest checks exception reporting, bad pointers, output budgets, services, deterministic ticks and bounded loops.
- [x] Commits: [13dda686](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/13dda68655c4b6d8c66dda7baf19f39a89ba84aa) (ADR/ADRP + flags); [c83cc965](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/c83cc96557b9a7fa63766436d6ec44913904bd90) (synthetic guest runtime), followed by CLI/fixture/docs commit.

**Still missing:** real Horizon SVC numbers, IPC and service behavior, scheduler, kernel, GPU, JIT and hardware evidence. The synthetic interface is not a substitute for native Switch 2 behavior. No Windows GitHub Actions results were verified at the time of this milestone.
