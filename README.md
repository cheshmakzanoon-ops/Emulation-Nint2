# Emulation-Nint2

**Independent experimental AArch64 user-mode emulation project researching future Nintendo Switch 2 application compatibility. It cannot run Nintendo games or firmware.**

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
requiring an AArch64 compiler on CI. **The new CI runs are pending verification.**
This test-only synthetic ABI does not establish Switch 2 game compatibility.

## Limitations and development

No emulator of Nintendo proprietary formats or Horizon APIs, no JIT or GPU. Unsupported guest instructions and malformed executables are rejected explicitly. See the 61-phase plan in docs/MASTER_DEVELOPMENT_PLAN.md, current status in docs/STATUS.md, architecture in docs/ARCHITECTURE.md, provenance in docs/PROVENANCE.md, and guidelines in AGENTS.md.

All included fixtures are original. No Nintendo firmware, games, keys, proprietary SDKs, assets or bypass methods are included. Independent project; not associated with Nintendo or NVIDIA. Final repo license pending.
