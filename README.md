# Emulation-Nint2

**Independent experimental AArch64 user-mode emulation project researching future Nintendo Switch 2 application compatibility. It cannot run Nintendo games or firmware.**

## Working ELF64/AArch64 execution milestone

The reference interpreter executes original ARM64 instructions with guest page permissions. A bounded static ELF64 loader maps PT_LOAD segments. Original program: MOVZ W0,#7; ADD W0,W0,#35; BRK #0. The result is x0=42 after 3 instructions.

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

## Limitations and development

No emulator of Nintendo proprietary formats or Horizon APIs, no JIT or GPU. Unsupported guest instructions and malformed executables are rejected explicitly. See the 61-phase plan in docs/MASTER_DEVELOPMENT_PLAN.md, current status in docs/STATUS.md, architecture in docs/ARCHITECTURE.md, provenance in docs/PROVENANCE.md, and guidelines in AGENTS.md.

All included fixtures are original. No Nintendo firmware, games, keys, proprietary SDKs, assets or bypass methods are included. Independent project; not associated with Nintendo or NVIDIA. Final repo license pending.
