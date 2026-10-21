# Emulation-Nint2

**Independent experimental AArch64 user-mode emulation project researching future Nintendo Switch 2 application compatibility. It cannot run Nintendo games or firmware.**

## Latest verified milestone: two compiled C processes

Two independently compiled AArch64 C programs now complete **64 bidirectional
request/reply transactions** through two capacity-one queues. Both validate
packet sequence, payload and checksum, enforce direction-restricted queue
capabilities, close their handles and exit zero. The implementation includes
private address spaces, deterministic scheduling, real queue backpressure and
an independent host digest check. Full results and historical progress are in
[STATUS.md](docs/STATUS.md).

Run the demo with any supported native C++20 toolchain; no ARM64 compiler is
needed for the checked-in original fixture images:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure
./build/nint2 --run-synthetic-pair build/compiled_rpc_client.elf build/compiled_rpc_server.elf
```

For a Windows Visual Studio build, the final command is:

```powershell
.\build\Debug\nint2.exe --run-synthetic-pair build/compiled_rpc_client.elf build/compiled_rpc_server.elf
```

Expected stdout:

```text
C RPC client: 64 replies verified!
C RPC server: 64 requests processed!
```

The normal CLI run takes **10,873 guest instructions** with the recorded
Clang/LLD 18 fixture images; both exits are zero. The integration harness uses
additional empty-queue priming and split budgets, so its instruction counts
are different. It tests quanta 1, 7, 64 and 257 twice each and requires identical
replay results. Both guest digests equal `0x73ef55c8d67d7c57`.

To reproduce the independent compiler-source verification on Linux with
Clang/LLD/LLVM 18 installed:

```sh
python3 tests/verify_assembler_fixtures.py --clang clang-18 --objcopy llvm-objcopy-18
python3 tests/verify_compiled_guests.py --build-dir build --clang clang-18 --lld ld.lld-18 --objdump llvm-objdump-18 --check-rpc-fixtures
```

This compiles and executes four original single-process C guests, the original
assembly sender/receiver pair, and the compiled C request/reply pair. The normal
build remains offline and requires no cross-compiler. RPC guest compilation
uses `-O1 -mgeneral-regs-only`, consistent with the interpreter's current integer
scope. The new optional synthetic QueueWait service and sealed asset runtime below are
not yet verified by the queued GitHub Actions workflow. Native blocking kernel
primitives, JIT, graphics and Switch 2 interfaces remain future work.

## In-progress milestone: resumable IPC and sealed guest assets

On October 21, 2026, the following implementation was **pushed to main**.
Build and CTest results for these changes are not yet confirmed: the remote
workflow is queued. Do not confuse this with native Nintendo compatibility.

- **Cooperative queue waits (SVC 9):** a guest either observes readiness,
  receives an explicit peer-closed result or suspends at its SVC. The scheduler
  skips blocked processes and reports deterministic deadlock instead of spinning.
  Final process exits retire queue endpoints without losing diagnostic handles.
- **Hermetic file images (SVC 10–17):** host-mounted immutable assets have
  bounded paths, per-process handles, stable sorted enumeration, deterministic
  FNV-1a digests, independent seek offsets and atomic guest-memory reads.
- **Guest heap (SVC 18–21):** sparse, private and deterministically placed
  virtual pages with W^X checked protection, safe metadata queries, explicit
  Free and process-exit/fault cleanup. Original ARM64 heap and heap+asset
  programs exercise these end-to-end.
- **Packet-size queue waits:** optional Send x3 size prevents wake-ups before
  enough bytes for the complete message are available.
- **New ELF-to-asset CLI:** the host explicitly imports a single file and
  a source-controlled ARM64 guest reads it using its virtual descriptor.
  No file opened by guest code maps to the host filesystem.

Build and run the source-authored example:

```sh
cmake -S . -B build -DNINT2_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/nint2 --run-synthetic-asset build/original_asset_guest.elf \
  /demo/message tests/fixtures/asset_message.txt
```

When these tests pass, the expected stdout is `Loaded from asset!` plus
one newline. The ELF is produced by the `generate_original_asset_elf`
CTest fixture. Optional independent assembly parity is checked by
`tests/verify_assembler_fixtures.py`. Additional details are recorded
in [SYNTHETIC_ABI.md](docs/SYNTHETIC_ABI.md).

## AArch64 CPU continuation (source-published, full CI pending)

The reference interpreter has since added original AArch64 exclusive
and LSE atomic operations (including 64/128-bit paired operations),
EL0 NZCV and TPIDR_EL0 register access, and CRC32/CRC32C instructions.
The architecture conformance corpus now includes independent Clang
assembly and freestanding compiled C guests, with checks for permissions,
atomic alias generations, register semantics and golden checksum values.

**Validation status:** the CRC interpreter passed targeted local C++20
conformance checks (8,192 deterministic vectors and canonical digests),
and the original CRC guest cross-compiled successfully. The combined
current-branch Linux/Windows/sanitizer CI suite is not yet confirmed
passing, so these additions must not be described as a fully validated
breakthrough. See [architecture and test record](docs/CPU_ATOMIC_CRC_MILESTONES.md)
and [status](docs/STATUS.md). Nintendo console ABI, firmware and game
compatibility remain unimplemented.

## Working ELF64/AArch64 execution milestone

The reference interpreter executes original ARM64 instructions with guest page permissions. A bounded static ELF64 loader maps PT_LOAD segments. Original program: MOVZ W0,#7; ADD W0,W0,#35; BRK #0. The result is x0=42 after 3 instructions.

## Breakthrough: an original guest program writes output and exits

An independently authored AArch64 ELF can now call a **synthetic test-only** service interface, write a message from guest memory, and exit. The original ARM64 assembly is in `tests/fixtures/original_hello.s`. This is a CPU + ELF loader + sandboxed guest-service integration test, **not** Nintendo Switch 2 Horizon emulation.

Build and test with CTest; it now generates an original ELF fixture into the build folder and executes it using `--run-synthetic`. See [synthetic guest ABI](docs/SYNTHETIC_ABI.md) for the exact documented calling convention and limits.

## Original AArch64 guest IPC breakthrough

The emulator now has a **synthetic**, capability-scoped handle table and bounded in-memory message queues. An independently assembled AArch64 ELF can create a queue, send a message, receive it into separate writable guest memory, print it and exit normally. This exercises executable loading, CPU interpretation, guest memory, permission checking, a miniature deterministic test kernel and message delivery end-to-end.

The entire path is **original test code, not Horizon OS IPC or Switch 2 game compatibility**. See [synthetic ABI](docs/SYNTHETIC_ABI.md) and [implementation status](docs/STATUS.md).

## Build and test

Requires CMake 3.24+ and C++20 (MSVC on Windows; GCC or Clang on Linux). No device firmware, game files or special dependencies.

~~~sh
git clone https://github.com/cheshmakzanoon-ops/Emulation-Nint2.git
cd Emulation-Nint2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
./build/nint2 --cpu-smoke
~~~

Optional: produce the independently authored AArch64 ELF with Clang+LLD:

~~~sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld -Wl,-Ttext=0x10000 -Wl,--build-id=none tests/fixtures/original_42.s -o original_42.elf
./build/nint2 --run-elf original_42.elf
~~~

Expected: Original ELF execution: stop=1 x0=42 steps=3 pc=0x10008.
On Windows MSVC builds, run build/Debug/nint2.exe instead of ./build/nint2.

## Original synthetic guest ELF demo

With optional Clang/LLD cross-compilation:

~~~sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld -Wl,-Ttext=0x10000 -Wl,--build-id=none tests/fixtures/original_hello.s -o original_hello.elf
./build/nint2 --run-synthetic original_hello.elf
~~~

Expected stdout: `Hello from AArch64!` followed by a newline. The CLI reports `exit=0 steps=8` on stderr. Windows MSVC builds run the equivalent executable from `build/Debug/nint2.exe`.

## Original guest IPC demonstration

Using optional Clang with the AArch64 target and LLD:

~~~sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096 tests/fixtures/original_queue.s -o original_queue.elf
./build/nint2 --run-synthetic original_queue.elf
~~~

Expected: `Hello via kernel!`, then synthetic exit=0 and steps=25. CTest independently generates an original queue ELF in the build folder and verifies the normal CLI output even without cross-compilation tooling.

## Compiled C guest: interpreter + stack + ELF + synthetic services

The reference interpreter now executes **ordinary Clang-generated, freestanding AArch64 C code**—not only handcrafted assembly. The [independently authored C fixture](tests/fixtures/original_compiled_guest.c) computes a 23-byte weighted checksum using a loop and multiplication, compares it with 19103, calls a synthetic Write service, and exits with 0. It exercises function-call frames, paired/unscaled/register-offset loads and stores, conditional selects, signed/unsigned bitfields, MADD/MSUB, and branch instructions. Replacing one guest string byte causes the checksum test to exit 99 without output.

**No Nintendo binaries or Horizon calls are involved.** Generated ELF test bytes are assembled into the build folder from original machine words embedded in the host C++ test, keeping Windows CI independent of cross-compilers.

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

# Host test executable emits the original guest ELF (Linux single-config example).
./build/nint2_compiled_c_tests --emit-elf build/compiled_c_guest.elf
./build/nint2 --run-synthetic build/compiled_c_guest.elf
~~~

Expected guest stdout: `Compiled C on AArch64!` and the CLI reports `exit=0 steps=519`. Use `build/Debug/nint2_compiled_c_tests.exe` and `build/Debug/nint2.exe` for Visual Studio generators.

The equivalent independently authored source may be cross-compiled using Clang/LLD:

~~~sh
clang --target=aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -O0 -fuse-ld=lld -Wl,-Ttext=0x10000 -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,-e,_start tests/fixtures/original_compiled_guest.c -o build/original_compiled_guest.elf
./build/nint2 --run-synthetic build/original_compiled_guest.elf
~~~

## Continued compiler and interpreter development (2026-10-21)

The AArch64 reference interpreter now includes signed scalar loads (LDRSB, LDRSH,
LDRSW with scaled, unscaled and register-offset forms, including pre/post-indexing),
plus RBIT, REV16, REV32, REV, CLZ and CLS. Tests include independently assembled
encodings, memory-fault rollback, reserved instruction traps and 1,024 deterministic
leading-zero test vectors. SIMD remains unsupported and must trap.

An independently authored **freestanding C guest IPC** fixture performs a signed
8-bit by signed 16-bit dot product, creates a test-only queue, sends 22 bytes,
receives into writable BSS, verifies every byte, writes to stdout, closes the
handle and exits. A tampered signed constant must cause exit 91 before queue
creation. Source: **tests/fixtures/original_compiled_ipc.c**. CTest generates its
own ELF in the build directory using **tests/compiled_ipc_guest_tests.cpp**.

To cross-compile the original C guest independently on Linux with Clang 17/LLD:

~~~sh
clang --target=aarch64-none-elf -O0 -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-unwind-tables -nostdlib -fuse-ld=lld \
  -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096 \
  tests/fixtures/original_compiled_ipc.c -o build/original_compiled_ipc.elf
./build/nint2 --run-synthetic build/original_compiled_ipc.elf
~~~

Expected stdout: C guest IPC verified! followed by a newline; expected exit 0.
CTest checks original_compiled_c_ipc and cli_compiled_ipc_guest offline, without
requiring an AArch64 compiler on CI. **Local full-runtime validation now passes; remote CI is tracked separately.**
This test-only synthetic ABI does not establish Switch 2 game compatibility.

## Limitations and development

No emulator of Nintendo proprietary formats or Horizon APIs, no JIT or GPU. Unsupported guest instructions and malformed executables are rejected explicitly. See the 61-phase plan in docs/MASTER_DEVELOPMENT_PLAN.md, current status in docs/STATUS.md, architecture in docs/ARCHITECTURE.md, provenance in docs/PROVENANCE.md, and guidelines in AGENTS.md.

All included fixtures are original. No Nintendo firmware, games, keys, proprietary SDKs, assets or bypass methods are included. Independent project; not associated with Nintendo or NVIDIA. Final repo license pending.

## Original two-guest ELF + deterministic scheduler experiment (2026-10-21)

The experimental runtime now includes an **entirely synthetic** host-controlled
round-robin scheduler (1–16 guest sessions) and receive-only capability sharing
between separately instantiated guest address spaces. In addition to the
direct-instruction regression test, two independently authored AArch64 assembly
programs can be linked to two separate ELF64 executables, loaded by the actual
ELF loader and run through the interpreter and synthetic IPC services.

One guest creates a queue and sends `PING!`; the other first observes the
synthetic nonblocking `-EAGAIN` result, retries using ARM64 branches, receives
the message and writes it to captured stdout. The sender closes its handle
without invalidating the receiver's attenuated capability. This is an **original
test-only ABI**, not Horizon service emulation, a Switch 2 boot or game support.

Run the default CTest suite to exercise the source-encoded pair without an
AArch64 toolchain. To additionally check independently linked ELF binaries:

~~~sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld \
  -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096 \
  tests/fixtures/original_sender.s -o build/original_sender_linked.elf
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld \
  -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096 \
  tests/fixtures/original_receiver.s -o build/original_receiver_linked.elf
./build/nint2_multi_guest_elf_tests --run-pair \
  build/original_sender_linked.elf build/original_receiver_linked.elf
~~~

This invocation is suitable for Linux single-config builds. On Windows use the
corresponding Debug test executable and prebuilt in-tree ELF fixtures. Linux CI
also executes the independent cross-compiled pair after CTest. Local validation
now confirms both guest exits and the PING! exchange; see the latest status
entry for remote CI results.

## Integer ISA expansion and original-source parity (2026-10-21)

New original interpreter operations: `UDIV`, `SDIV`, `LSLV`, `LSRV`,
`ASRV`, `RORV`, `ADC`, `ADCS`, `SBC`, `SBCS`, `EXTR` and the
`ROR` immediate alias, including 32/64-bit forms and defined divide-by-zero
and carry/borrow edge cases. They use architectural bitmask and NZCV behavior;
unallocated encodings must trap.

`tests/cpu_divide_shift_tests.cpp`, `tests/cpu_carry_tests.cpp`, and
`tests/cpu_extract_tests.cpp` provide independently authored assembly
encodings, reserved-opcode cases and deterministic host-oracle vectors.
Optimized freestanding C examples in `tests/fixtures/original_division_c.c`
and `tests/fixtures/original_ror_c.c` are cross-compiled to AArch64 ELFs
and executed by the normal `--run-synthetic` CLI locally and in the Linux CI
configuration. Local execution has been verified with exit 0 and exact output.

The separate source/encoding check can run on a Linux machine with Clang,
llvm-objcopy and Python 3, without any console software:

~~~sh
python3 tests/verify_assembler_fixtures.py
~~~

This compares nine checked-in host instruction vectors against original
Clang-assembled AArch64 source fixtures. CMake now registers 36 CTest entries.
See the latest status entry for executed results and outstanding platform gates.
