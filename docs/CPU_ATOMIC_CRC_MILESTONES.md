# AArch64 atomics, system registers and CRC conformance

**Status:** partial original AArch64 reference interpreter, not Nintendo
hardware/firmware emulation. Updated October 21, 2026.

## Scope and references

All mnemonic source in tests/fixtures is original code. No proprietary
console executables, console keys, firmware or restricted SDK were used.

- Arm Architecture Reference Manual, CRC instruction overview:
  https://support.arm.com/documentation/ddi0487
- Public ARM instruction encoding and operand descriptions:
  https://arm.jonpalmisc.com/latest_aarch64/crc32
  and https://arm.jonpalmisc.com/latest_aarch64/crc32c
- Clang AArch64 CRC compiler intrinsic tests:
  https://llvm.googlesource.com/clang/+/refs/heads/master/test/CodeGen/arm64-crc32.c

These sources establish ARM instruction semantics, not Switch 2 CPU timing,
Horizon OS interfaces, hardware cache topology or native device behavior.

## Implemented (synthetic reference scope)

### Atomic memory

- Exclusive scalar LDXR/LDAXR/STXR/STLXR byte, halfword, word and doubleword.
- Exclusive pair LDXP/LDAXP/STXP/STLXP for 32+32 and 64+64-bit pairs.
- LSE CAS, CASP and SWP; LDADD, LDCLR, LDEOR, LDSET, LDSMAX, LDSMIN,
  LDUMAX and LDUMIN, with acquire/release variants.
- Non-exclusive LDAR and STLR scalar loads/stores.
- CLREX; deterministic single-host-thread DMB/DSB/ISB barriers.

Aliases share physical frame storage and its mutation generation. A write
through any alias invalidates an earlier exclusive reservation for that
frame. STXR/STXP consume their reservation; a failed CAS comparison does not
mutate memory or generation. Pair writes remain all-or-nothing.

**Important limitation:** the emulator has one cooperative host execution
thread. These instructions do not imply native multicore ordering, native
cache coherency, device DMA visibility or GPU fences.

### EL0 system registers

MRS/MSR NZCV operates on condition flags in bits 31:28. MRS/MSR TPIDR_EL0
stores a per-guest thread pointer without exposing a host-process address.
Unrecognized and privileged MRS/MSR encodings trap explicitly; the test
runtime does not synthesize real CPU clocks, FP state or kernel registers.

### CRC checksum instructions

CRC32B/H/W/X use the reversed IEEE polynomial 0xEDB88320. CRC32CB/CH/CW/CX
use the reversed Castagnoli polynomial 0x82F63B78. Each instruction receives
the running 32-bit CRC explicitly and processes low-order input bytes first;
there is no implicit initial/final XOR. CRC32X/CX read the entire Xm value.
Wd is zero-extended; NZCV remains unchanged. SF/width-invalid words trap.

The implementation uses unsigned C++ operations, independent of native CPU
intrinsics and host endianness.

## Original fixture coverage

| Original source | Test | Verified source words |
| --- | --- | --- |
| original_atom.s | cpu_exclusive_tests.cpp | 22 |
| original_lse.s | cpu_lse_tests.cpp | 42 |
| original_pair.s | cpu_pair_tests.cpp | 17 |
| original_sysregs.s | cpu_system_tests.cpp | 12 |
| original_crc.s | cpu_crc_tests.cpp | 11 |
| original_lse_guest.c | compiled_lse_guest_tests.cpp | Clang-generated ELF |
| original_crc_guest.c | verify_compiled_guests.py | Clang-generated ELF |

The assembly verifier compares actual Clang AArch64 encodings against test
arrays. The C-guest verifier additionally requires genuine compiler-emitted
atomic and CRC mnemonics instead of an untested helper function.

## Results actually verified in the continuation environment

- ARM64 source parity matched the exclusive, LSE, paired and system-register
  corpora: 22/42/17/12 original instructions; embedded compiled LSE machine
  words matched the independently assembled Clang 17 ELF.
- Clang 17 assembled eleven original CRC instructions.
- The portable CRC C++ implementation compiled with C++20, Clang and strict
  warnings (-Wall -Wextra -Wpedantic -Werror), using a small functional host
  interpreter harness independent of the full emulator build.
- The targeted CRC harness passed 8,192 randomized per-instruction vectors,
  both canonical 123456789 checksum results (0xcbf43926 and 0xe3069283),
  instruction-executed golden checks, SF/size traps, NZCV preservation,
  X-width source checks, and aliased source/destination register checks.
- A freestanding Clang/LLD AArch64 CRC C guest compiled. The resulting ELF
  disassembly contained CRC32B, CRC32CB, CRC32X and CRC32CX.
- **The latest full main CMake build, CTest, Windows and sanitizer tests are
  not yet verified.** GitHub Actions runs are queued. No job pass claimed.

## Reproduce

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNINT2_BUILD_TESTS=ON
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure

python3 tests/verify_assembler_fixtures.py --clang clang-18 --objcopy llvm-objcopy-18
python3 tests/verify_compiled_guests.py --build-dir build \
  --clang clang-18 --lld ld.lld-18 --objdump llvm-objdump-18 \
  --check-rpc-fixtures
~~~

Cross-compilation checks require Clang, LLD and LLVM 18 on Linux; normal
offline CTest is designed not to require an ARM64 cross compiler.

## Outstanding milestones

A full AArch64 SIMD/FP ISA, privileged architecture, real memory ordering,
verified hardware timing, console OS calls, native executable formats, JIT,
GPU, audio, input, interrupts, firmware boot and actual game compatibility
are all outside this tested reference stage. No DRM or signature bypass is
provided or claimed.
