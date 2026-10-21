# Emulation-Nint2

**Experimental, independent Nintendo Switch 2 user-mode emulation research project.** Native Switch 2 software compatibility is a long-term objective, **not a current feature**.

> **Current state (M0 early bootstrap):** a C++20 desktop command-line application, a small page-mapped guest-memory proof of concept, CTest tests and a 61-phase development plan. **No AArch64 guest execution, Horizon HLE, GPU emulation, Nintendo firmware or playable games yet.**

## Project documents

- [Master development plan — 61 phases](docs/MASTER_DEVELOPMENT_PLAN.md): technical architecture, exact phase work, tools, dependencies and exit criteria.
- [Hardware research protocol](docs/HARDWARE_RESEARCH.md): what is publicly documented, unknown, safely observable and testable.
- [Architecture decisions](docs/ARCHITECTURE.md): core module boundaries and reasons for the technical choices.
- [Actual implementation status](docs/STATUS.md): implemented vs planned capabilities and verification.
- [Contributor and agent instructions](AGENTS.md): development and evidence requirements.

## Compile and test

Requires CMake 3.24+ and a C++20 compiler (Visual Studio/MSVC or Clang on Windows; GCC/Clang on Linux). The initial build **does not need** Vulkan, SDL, Dynarmic, Switch hardware, firmware, keys or games.

~~~sh
git clone https://github.com/cheshmakzanoon-ops/Emulation-Nint2.git
cd Emulation-Nint2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
~~~

Linux/Unix native run:

~~~sh
./build/nint2 --help
./build/nint2 --version
./build/nint2 --smoke
~~~

On Windows Visual Studio builds, the executable is normally build/Debug/nint2.exe. The smoke command checks **host-native memory APIs only**; it does not interpret ARM64 instructions.

## Technical direction

C++20 + CMake, an independently written AArch64 reference interpreter, a vetted/pinned ARM64 dynamic recompiler, sparse memory/guest scheduling, version-aware Horizon-style high-level OS emulation and Vulkan rendering from **independently verified** GPU commands and shader behavior. A synthetic host renderer can be built before proprietary GPU instructions are understood. Each step must have an original program or fixture and honest compatibility reporting.

## Copyright, device security and provenance

No Nintendo firmware, games, console keys, proprietary assets, protected traces or undocumented confidential SDK materials are distributed. Investigations use public information, independently written fixtures, and lawfully authorized behavior of owned hardware. The project does not instruct contributors to compromise console security or online services and is not associated with Nintendo or NVIDIA.

**Final repository license has not been selected.** It must be resolved before accepting external contributions or integrating code with incompatible licenses.
