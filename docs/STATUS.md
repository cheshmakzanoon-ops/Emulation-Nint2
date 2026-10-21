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


## 2026-10-21 incremental kernel and guest IPC milestone

**Breakthrough: original AArch64 guest ELF executes the complete synthetic queue Create → Send → Receive → Write → Close → Exit path**, crossing CPU, ELF loader, memory permissions, session dispatch, bounded message queue and handle table.

- Commit [a6306e4](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/a6306e4de887f2be96ab0da5eb2a90f3eec502b6): capability-scoped handles, non-reusable IDs, duplicate-right subset checks, bounded FIFO IPC queues with empty/full/oversized tests.
- Commit [4c81b78](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/4c81b7812d10bf3a52e2178608dadc0b9563ae1a): synthetic services 4–8, original AArch64 queue ELF fixture, guest-buffer validation, failure-preserving receive semantics and atomic cross-page writes.
- CI/README/documentation follow-up: automatic fixture generation and CLI end-to-end stdout check.

**Validation performed locally on Linux x64, 2026-10-21:** all **9/9** test groups passed under GCC, Clang, GCC UBSan and GCC ASan+UBSan against the local reconstructed/updated runtime (the connected repository also has three earlier guest-session/CLI tests not represented in that local 9-test count). Independently Clang+LLD-linked original_queue.s executed via ELF loader + interpreter + guest synthetic calls: stdout `Hello via kernel!\n`, exit=0, stop=0, steps=25, handles=0. Cross-page invalid-destination test confirms non-consumption and unmodified preceding bytes.

**CI status and limitations:** GitHub Actions for the new commit must be checked separately; no Windows or remote CI success is claimed without a completed run. Still **not** native Switch 2 compatibility. No verified Horizon IPC, kernel service numbers, threading, real scheduling, GPU implementation, title formats, firmware or retail games. The outbound license remains undecided.

**Next required work:** independent multi-process/cross-session queue semantics and deterministic scheduling, stronger CPU instruction conformance, parser fuzz coverage, documented service research, and Vulkan-only host rendering experiments. No undocumented native device behavior may be guessed from the synthetic ABI.

## 2026-10-21 compiler-generated AArch64 C milestone

**Breakthrough (original C, no Nintendo interfaces):** a freestanding C application compiled with Clang 17 for `aarch64-none-elf` executes through the integer interpreter, guest memory, stack, and synthetic Write/Exit calls. It traverses a 23-byte input with a C `for` loop and a weighted checksum, exercises subroutine calls/returns and stack frames, prints `Compiled C on AArch64!\n`, and exits 0 after **519 guest instructions**. Mutating a single source-data byte changes the checksum and takes the original guest's error path: exit 99, no stdout, **488 guest instructions** in the local harness.

Implemented during this increment:
- [x] Added `src/core/cpu_extended.cpp`: TBZ/TBNZ, STP/LDP pairs (signed offset, pre/post indexing), STUR/LDUR and signed pre/post indexing, scalar register-offset LDR/STR (UXTW/LSL/SXTW/SXTX), scalar LDR/LDRSW literal, conditional-select family (CSEL/CSINC/CSINV/CSNEG), MADD/MSUB, signed/unsigned bitfield moves (SBFM/UBFM). Explicitly reject unsupported SIMD and constrained-unpredictable forms rather than silently emulating them.
- [x] Guest pair stores use the existing `GuestMemory::WriteBuffer` all-or-nothing validation so a mapping failure cannot partly overwrite a second guest page; pair loads stage both words before writing destination registers. Indexed base writeback is not committed on data faults.
- [x] New `tests/cpu_extended_tests.cpp` and independent AArch64 assembly `tests/fixtures/original_integer_extensions.s` cover decoded instructions, branching, sign extension, register flags, and first/second-word pair faults.
- [x] New `tests/fixtures/original_compiled_guest.c` source and `tests/compiled_c_guest_tests.cpp` with reproducible cross-assembled machine words in source. CTest now runs the compiled C guest, generates an original ELF in the build folder, and checks the `--run-synthetic` CLI output, without needing Clang on CI.
- [x] Fixed a real previously observed MSVC compilation failure (`src/core/session.cpp` used `std::length_error` without including `<stdexcept>`). This does not by itself establish a passing Windows CI run.

**Checks actually executed locally:** Clang 17 + LLD cross-compilation of the original C ELF; disassembly with `llvm-objdump`; independent host harness with the changed interpreter and a simplified permission-checked test guest memory implementation: 519-step positive result and 488-step negative result. Focused extended-ISA fault tests passed under Clang++ 17 with AddressSanitizer + UndefinedBehaviorSanitizer; ELF emitted by the new test-code generator was executed successfully by that harness, and its mutated guest constant rejected. A locally reproduced version of the committed original C source yielded identical AArch64 `.text` bytes under the same compiler/linker options.

**Not yet verified:** this increment has not executed the entire *connected repository* CTest matrix locally, and the newest GitHub Actions Windows/Linux/sanitizer runs were still queued when inspected. An older Windows build from commit `3ed8a5c` failed due to the absent standard include; the corresponding Linux and sanitizer jobs passed. No new Windows result is claimed until CI completes. These tests validate only a **narrow reference interpreter and original synthetic services**, not Nintendo Switch 2 firmware, kernel IPC, titles, GPU, or game compatibility.

**Next highest-value tasks:** wait for CI execution and correct any further MSVC-specific errors; add signed/unsigned loads and data-processing instruction families using independently authored compiler fixtures; expand differential ISA/property tests and loader mutation corpus before attempting a JIT.
