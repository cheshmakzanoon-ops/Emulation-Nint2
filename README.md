# Emulation-Nint2

**Experimental independent AArch64 user-mode emulation research project targeting Nintendo Switch 2-compatible applications in the long term.** It is **not** capable of running Nintendo Switch 2 firmware or games.

## Current executable milestone

The project includes a portable C++20 sparse-page guest-memory layer and an original reference AArch64 interpreter. The interpreter supports a narrow subset of integer instructions, branches and 8–64-bit loads/stores. It executes an original machine-code smoke test that computes **7 + 35 = 42**, then stops at a BRK instruction.

This is **CPU proof of execution**, not game or Horizon compatibility. The interpreter deliberately faults on unsupported instructions, unexecutable pages and invalid memory.

## Build and verify

Requires CMake >=3.24 and C++20 (MSVC/Clang on Windows; GCC/Clang on Linux). No firmware, console images, keys or extra runtime dependencies.

~~~sh
git clone https://github.com/cheshmakzanoon-ops/Emulation-Nint2.git
cd Emulation-Nint2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
./build/nint2 --cpu-smoke
~~~

Windows Visual Studio builds generally run 'build/Debug/nint2.exe --cpu-smoke'.

Expected output: 'AArch64 interpreter smoke passed: x0=42 steps=3'.

## Documentation

- [Master development plan — 61 phases](docs/MASTER_DEVELOPMENT_PLAN.md)
- [Actual implementation and test status](docs/STATUS.md)
- [Architecture decisions](docs/ARCHITECTURE.md)
- [Hardware research methodology](docs/HARDWARE_RESEARCH.md)
- [Provenance and rights](docs/PROVENANCE.md)
- [Contributor/agent rules](AGENTS.md)

## Copyright and hardware claims

The repository contains original code and original assembly fixtures only. No Nintendo firmware, games, keys, protected dumps, SDK, DRM bypass procedures or copyrighted assets are included. Switch 2-specific reverse engineering requires lawfully obtained independent observations, documented uncertainty and reproducible evidence. No external libraries or prior emulator code have been imported.

**Final project license is undecided.** The repository is not associated with Nintendo or NVIDIA.
