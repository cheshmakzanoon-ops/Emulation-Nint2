# Implementation status — 2026-10-21

**Stage: original AArch64 runtime foundations with partial memory, interpreter, ELF, synthetic process and IPC support. Latest local validation: 36/36 CTests under GCC, Clang and GCC ASan+UBSan with leak checks; two compiled C processes complete 64 request/reply transactions. Native Nintendo Switch 2 compatibility remains unimplemented.**

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
3. Extend the tested deterministic synthetic sessions/IPC with researched lifecycle and synchronization semantics; blocking waits, peer notifications and a JIT remain unfinished.
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


## 2026-10-21 signed-load, compiled C IPC and bit-operation increments

- **Pushed main commit [7be06ed](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/7be06ed7695027a26589735d5e3d689bcc614f01):** signed LDRSB/LDRSH/LDRSW (scaled, unscaled and register offsets), writeback/fault preservation, W/X extension rules, reserved-opcode tests and SIMD-scalar opcode collision fix.
- **Pushed main commit [a348ebe](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/a348ebe6df24963e4382d764133bf268fa5d87e1):** independently authored Clang-generated C guest tests signed byte/halfword multiplication, creates a synthetic queue, sends/receives 22 bytes through guest memory, compares, writes output, closes handle and exits; modifying one signed source constant exercises exit 91. CI does not need Clang: ELF machine words are embedded in test source and emitted into the build directory.
- **Pushed main commit [a83d5eb](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/a83d5ebbe4896e9e8ef433916697a3ba6ad2efa1):** RBIT, REV16, REV32, REV, CLZ and CLS; original cross-assembled vector tests, 1,024 deterministic leading-zero oracle comparisons, rejected reserved encodings.

**Checks actually performed:** Clang 17 cross-assembled both original_signed_loads.s and original_bit_ops.s for aarch64-none-elf and llvm-objdump decoded the encodings. Clang 17/LLD built original_compiled_ipc.c; readelf, llvm-objdump and llvm-objcopy inspected its sections, code words and constants. All three new GitHub commits were confirmed pushed to main.

**Remote CI baseline:** [f7b5c5a workflow](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/actions/runs/39553809711) completed with Linux, Windows and sanitizer jobs all successful. **No full CMake/CTest run for the newly pushed increments has been verified yet.** The authoring environment could not clone github.com due to DNS restrictions; GitHub Actions for the new commits were queued at the last check. Do not claim successful new native guest execution, exact instruction counts, Windows compilation or sanitizers until CI completes.

**Next gates:** inspect queued CI failures and fix any cross-platform regressions; independently compare linked C binary execution with the source-embedded regression ELF; then extend instruction conformance and deterministic synthetic scheduling. This project still has no Switch 2 Horizon ABI, device evidence, shader/GPU implementation or game compatibility.

## 2026-10-21 continued development: logical decoding and two-ELF scheduling

Five incremental code/CI commits pushed to `main`:

1. [eea67d9](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/eea67d9db87e2be402df5e1be981e0c776360cd7): `cpu.cpp`, `cpu_extended.cpp`, original logical assembly fixture, logical instruction/regression test, CMake. Implemented AND/ORR/EOR/ANDS logical immediates (A64 replicated rotated bitmasks, invalid-encoding rejection, NZCV), plus inverted operand shifted-register BIC/ORN/EON/BICS.
2. [da867ed](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/da867ed960632c8f046347223860f83e42cf4e85): `kernel.hpp`, `kernel.cpp`, kernel tests. Host-side `HandleTable::GrantTo` clones an attenuated shared-queue capability into a separate handle namespace only with Duplicate right; source handle remains intact; target exhaustion and stale handle checks.
3. [736d6b6](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/736d6b6347b02e4e92dd1476f0b21c3babe39f00): `scheduler.hpp`, `scheduler.cpp`, `session.hpp/.cpp`, `scheduler_tests.cpp`, `original_sender.s`, `original_receiver.s`, CMake. Host-only deterministic resumable round-robin time slices with an aggregate instruction limit, per-guest faults/exits and two isolated original A64 guest processes exchanging a message.
4. [16ca699](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/16ca6998298e94d7a91e02d1eb05cd1096ceb26f): `multi_guest_elf_tests.cpp`, CMake and Linux CI. Two independent ELF64 images enter through the real bounded ELF loader and exercise host capability grant, IPC retry, message transfer, process exit and memory isolation; tests can emit source-encoded ELF files and reload them or accept independently linked external ELF files.
5. [f6ab691](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/f6ab691ce8768f4a063bb5e780ef7cf3a222c378): fix Bash continuation syntax in new CI cross-compilation step.

**Checks performed, not overstated:**
- Host environment had Clang 17, LLVM objdump and LLD; `original_logical_ops.s`, `original_sender.s`, and `original_receiver.s` were cross-assembled. Their produced ARM64 encodings were inspected with `llvm-objdump -d`.
- Original sender and receiver were each independently linked with Clang/LLD, and `readelf -l` confirmed static AArch64 ELF executables with an executable PT_LOAD, 0x10000 entry and no dynamic linking.
- A separate Python implementation decoded the eight new valid logical-immediate opcodes to their expected masks and rejected the three reserved test encodings.
- CMake now registers **26 test entries**, including new logical, isolated-session scheduler, memory-loaded two-ELF and file-reloaded two-ELF tests.
- **Not executed or verified yet:** a complete native CMake build and CTest suite for these new revisions, Linux/Windows Actions, ASan/UBSan tests, or actual success of the combined two-guest runtime. New GitHub Actions jobs were still **queued** at last inspection. Assembly/linker checks alone do not establish end-to-end execution.

**Next gates:** inspect completed CI results; immediately fix compiler and logic regressions, then validate identical results across source-encoded and independent Clang-linked ELF guest pairs; expand compiler-optimized instruction coverage and test fuzzing. Native Nintendo Switch 2 kernel, GPU, Horizon service ABI and game boot are not implemented.

## 2026-10-21 continued interpreter and CI quality work

All of the following commits were pushed to the existing **main** branch:

- [93d1584](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/93d158448a92e9503cf35cdc6ce1d1c1d1630651): interpreter support and a new 1,024-set CTest oracle for UDIV/SDIV/LSLV/LSRV/ASRV/RORV with defined zero-divisor, INT_MIN / -1 and modulo-width shift behavior.
- [b0ce0b2](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/b0ce0b26da0713b86c9ba7511df4ee7fbb5dd432): original optimized AArch64 C division test source and cross-compile/CLI execution gates in Linux CI.
- [4e0f88e](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/4e0f88e318e99875f49026a28e00f58a2c2c87f8): ADC/ADCS/SBC/SBCS, NGC/NGCS aliases, NZCV update, carry/borrow edge handling and a 1,024-vector carry-chain CTest.
- [95d1019](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/95d101989b4fc58e64fb8658230739f29a29a10d): EXTR and the ROR-immediate alias (including 32/64-bit forms), 2,048 randomized inputs per eight independently assembled A64 instructions, preserved flags and four invalid encodings.
- [f60cd7a](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/f60cd7ad105c1b5ad21f098f911fdc27e9967956): original optimized C guest that requires actual ROR #17 execution, with Linux CI compilation and synthetic output/exit check.
- [5938c7d](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/5938c7dc9248215f599a470c8ac64a4b5b02d67a): fix the disassembly grep expression in the new Linux CI step (independently checked against actual llvm-objdump output).
- [966fa91](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/966fa917c538d8700e311b318f0931ef09068c7b): Python stdlib original-assembly parity verifier plus Linux CI execution. It tests eight source/vector pairings including the multi-ELF producer and consumer cases.

**Verified locally in the authoring environment:**
- Compiled original A64 assembly source with Clang 17, extracted machine words with llvm-objcopy, and matched all eight selected source / host test vector pairings.
- Cross-compiled the optimized freestanding C division and ROR guests with Clang 17 / LLD and inspected the linked AArch64 ELF machine code. The latter emits `ror x8, x8, #0x11` after the source uses a volatile result to prevent an optimizing compiler from rewriting away the rotation.
- Calculated the optimized C guest's expected division, signed-shift and rotation constants independently, and exhaustively checked small-width (4-bit and 8-bit) addition/subtraction carry and overflow truth tables.
- A separate small C++20 oracle test evaluated 16,384 EXTR/ROR vector cases using original assembled instruction fields. This is **not** an execution of the complete repository's CMake or CTest suite.

**Not yet verified:** running the full current C++ emulator and CTest suite in this environment (direct GitHub cloning failed due DNS/network restrictions), a successful Linux/Windows/ASan/UBSan GitHub Actions run for these commits, or independently linked guests running successfully through the complete committed C++ runtime. As of this note the latest GitHub Actions run is queued. Do not count registered CTest entries (29) as passed tests.

**Next verification gate:** await actual Actions execution; inspect job logs and fix failures, then compare CI cross-linked guest traces against source-encoded fixture runs. These increments do not implement any native Nintendo Switch 2 firmware, Horizon services, proprietary container/format, GPU or game execution.


## 2026-10-21 full runtime verification and correctness repairs

The previously pending runtime gate was executed locally against the complete
repository on Linux x64 (GCC 13.3.0, CMake 3.28.3, Clang/LLD 18.1.3).
The unchanged `22093af` baseline built but passed **28/29** CTest entries: the
LSLV W-register test expected 3 for `0x80000001 << (33 & 31)`, although the
32-bit architectural result is 2. The literal oracle is corrected with an
explanation; all existing randomized vectors remain enabled.

This milestone (commit message `fix: validate ARM64 operands and recover queue allocation failures`):

- `src/core/cpu.cpp`, `cpu_extended.cpp`, new `cpu_sp_logical_tests.cpp` and
  `original_sp_logical.s`: correct SP/WSP reads for flag-setting immediate
  arithmetic, SP/WSP writes for non-flag-setting logical immediates, and ROR
  shifted logical operands. Three distinct failures were reproduced before the
  fixes. New tests include 4,096 deterministic rotate-oracle cases, zero/max
  rotations, NZCV, SP/ZR distinction, width truncation and invalid encodings.
- `include/nint2/kernel.hpp`, new `kernel_resource_tests.cpp`: remove an incorrect
  `noexcept` on allocating queue construction. The regression previously aborted
  with `std::bad_alloc`; it now returns OutOfMemory and preserves queue contents,
  capabilities and handle IDs across injected creation/send/grant failures.
- `include/nint2/session.hpp`, `src/core/session.cpp`, new
  `session_import_tests.cpp`: a host-only ImportQueue API provisions attenuated
  capabilities before executing guest code. Tests verify separate namespaces,
  rights, closed/stale handles, exhaustion/recovery and surviving queue references.
- `tests/multi_guest_elf_tests.cpp`: integration success requires both guest exit
  codes to be zero. Mutated sender/receiver programs that still transfer PING!
  but exit 7/9 are rejected.
- `CMakeLists.txt`, `tests/verify_assembler_fixtures.py`: register new tests and
  independently verify the new 17-word original SP/logical assembly fixture.

**Executed validation:**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNINT2_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 tests/verify_assembler_fixtures.py
```

Full CTest: **32/32 passed**. Original assembly/source parity: **9/9 mappings**
passed. Independently cross-compiled with Clang/LLD 18 and executed through the
actual CLI: original C checksum (**519 instructions**), original C IPC (**755**),
optimized C division (**47**), optimized C rotation (**24**), all with exact
expected stdout and exit 0. Independently linked sender/receiver ELFs exchanged
PING! with **24 scheduled instructions** and both exit codes zero. These close
the earlier local execution gaps; they do not imply completed remote CI.

The complete **32/32** CTest suite also passed GCC ASan+UBSan, configured
with `-fsanitize=address,undefined -fno-omit-frame-pointer`, and executed with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`. The approved execution
context provided the process-metadata access required by LeakSanitizer.
Windows and remote CI remain unverified. No native Switch 2 interfaces,
JIT, GPU or retail game execution is claimed.


## 2026-10-21 verified compiled-C client/server breakthrough

The reliability prerequisites were published to main as
[7b5063d](https://github.com/cheshmakzanoon-ops/Emulation-Nint2/commit/7b5063dbd85394cf715d690910ce9ac5beee81ea).
This milestone is recorded by `feat: run compiled ARM64 C client-server transactions end to end`.

**Before/after proof:** the independently compiled original client reaches
`EOR X18, X8, X1, ROR #13` (`0xcac13512`, PC `0x100ec`). The previous CPU from
`22093af` traps there and its peer reaches the instruction budget. The corrected
interpreter executes the complete request/reply application. This is tested
original compiler compatibility; it does not establish native console behavior.

### Implemented and reviewed

- `tests/fixtures/original_rpc_client.c`, `original_rpc_server.c`,
  `original_rpc.h`: independently authored C request/reply programs, 64 rounds,
  32-byte packets with sequence/payload/inverse/checksum, two outstanding
  requests, explicit errors, two wrong-direction permission checks per process,
  handle cleanup and success output. Compiled at `-O1 -mgeneral-regs-only`.
- `tests/fixtures/compiled_rpc_images.hpp`, `regenerate_rpc_fixtures.py`: compact
  source-encoded original PT_LOAD images and a reproducible Clang/LLD 18
  generator. Default CTest requires no cross-compiler or external runtime.
- `tests/compiled_rpc_guest_tests.cpp`: reconstruct/load original ELFs, provision
  send-only/receive-only queues, execute independent client/server sessions,
  validate host digest and both exit codes, exercise queue capacity one,
  private memory, deterministic replay, split budgets and explicit failures.
- `src/app/synthetic_pair.cpp/.hpp`, `src/app/main.cpp`, `CMakeLists.txt`:
  `--run-synthetic-pair client.elf server.elf` starts the original startup ABI,
  enforces a shared million-instruction budget and 64-instruction quantum,
  prints captured output and succeeds only when both guests exit zero.
- `tests/check_guest_cli.cmake`, `check_rpc_cli.cmake`: exact output plus process
  exit assertions. The former CTest regex-only property could accept correct
  stdout with a failing exit; a temporary regression emitting expected text and
  exiting 17 reproduced that false success and is rejected by the new wrapper.
  Windows CRLF is normalized; extra/missing output is still rejected.
- `.github/workflows/ci.yml`, `verify_compiled_guests.py`,
  `verify_assembler_fixtures.py`: Ubuntu 24.04 Linux CI selects Clang/LLD/LLVM 18,
  verifies original source/byte provenance, and executes all four independent
  single-process C examples plus the assembly and C process pairs. Existing
  Windows and sanitizer jobs run the expanded offline CTest suite.
- `README.md`, `docs/SYNTHETIC_ABI.md`, `docs/ARCHITECTURE.md`,
  `docs/PROVENANCE.md`: updated commands, contracts, evidence and limitations.

### Actual runtime observations

Normal CLI, recorded Clang/LLD 18 images, no priming or split-budget harness:

```text
C RPC client: 64 replies verified!
C RPC server: 64 requests processed!
Synthetic pair: stop=0 exited=2 failed=0 steps=10873
Client: state=exited exit=0 steps=5459 pc=0x10308
Server: state=exited exit=0 steps=5414 pc=0x102e8
```

Harness results below include an initial empty-queue receive and repeated
997-instruction aggregate budgets. Priming is tracked separately, explaining
the different CLI/harness instruction totals. Each schedule was replayed twice
with identical reports and instruction counts. Both guests completed 64
transactions with digest **`0x73ef55c8d67d7c57`**, two rights denials each,
zero remaining handles and zero exit codes.

| Quantum | Scheduled instructions | Full-queue send retries | Empty-queue receive retries |
| ---: | ---: | ---: | ---: |
| 1 | 11,162 | 0 | 181 |
| 7 | 11,134 | 0 | 179 |
| 64 | 11,302 | 51 | 140 |
| 257 | 31,854 | 393 | 1,266 |

Negative checks: corrupt request 7 causes server exit **114** after six completed
requests, no server success output, and the peer is bounded at 200,000 aggregate
instructions. A client that prints the success message then exits **77** is
rejected even though the scheduler completes. Report pointer zero works; private
sentinels at identical virtual addresses remain independent. The CLI rejects
non-ELF input.

### Validation actually executed

Linux x64, CMake 3.28.3, GCC 13.3.0, Clang/LLD/LLVM 18.1.3:

| Gate | Observed result |
| --- | --- |
| GCC Debug configure/build/full CTest | 36/36 passed |
| Clang Debug configure/build/full CTest | 36/36 passed |
| GCC ASan + UBSan + leak checking | 36/36 passed; no reported findings |
| Original assembly/source instruction mappings | 9/9 passed |
| Recorded RPC PT_LOAD bytes vs fresh Clang/LLD 18 output | Exact match |
| Independent compiled C / assembly / C process pairs | All expected stdout and zero exits; RPC negative suite also passed |

Commands executed include:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build/clang --parallel 4
ctest --test-dir build/clang --output-on-failure
python3 tests/verify_assembler_fixtures.py --clang clang-18 --objcopy llvm-objcopy-18
python3 tests/verify_compiled_guests.py --build-dir build --clang clang-18 --lld ld.lld-18 --objdump llvm-objdump-18 --check-rpc-fixtures
```

Sanitizer build uses `-fsanitize=address,undefined -fno-omit-frame-pointer`
plus matching linker flags, with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`
and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`. Approved execution
allowed LeakSanitizer process-metadata access. The first Clang test invocation
encountered three incomplete/non-executable local linker outputs; relinking
those generated files resolved the execution errors and the full suite passed.
No source change or test exclusion was used to address that local artifact issue.

**Limits and remaining gates:** Windows execution and latest GitHub Actions
completion must be checked separately; no remote success is inferred from
these local runs. Queue calls remain nonblocking; there is no peer-close
notification, blocking wait or deadlock detector. This is an original synthetic
runtime milestone, with no JIT, renderer, Horizon ABI, firmware or game support.
