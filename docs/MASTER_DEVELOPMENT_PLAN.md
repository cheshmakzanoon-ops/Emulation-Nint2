# EMULATION-NINT2 — MASTER DEVELOPMENT & HARDWARE RESEARCH PLAN

**Revision:** 1.0 • **Established:** 2026-10-21 • **Repository:** https://github.com/cheshmakzanoon-ops/Emulation-Nint2 • **Branch:** main • **Status:** foundational planning and bootstrap only

> This is a phased engineering and original research plan, **not** a claim that Switch 2 games can run. Nintendo Switch 2 native software interfaces, GPU protocols and executable ecosystems remain incompletely publicly documented. Treat unknowns as research tasks. Gates are evidence-based, not calendar promises.

## 1. Mission, architecture and completion definitions

**Mission:** Build an independently implemented, reproducible research-grade emulator of the Nintendo Switch 2 application environment, starting with original AArch64 programs and progressively emulating verified Horizon-style APIs and NVIDIA T239 GPU behavior. Aim for high compatibility long-term; optimize only after accuracy and reproducibility.

**First platform:** Windows 11 x86-64 (C++20, CMake, MSVC or clang-cl, Vulkan). **Second:** Linux x86-64 (Clang/GCC, Vulkan). **Future candidates:** Linux ARM64, macOS, Android after porting studies. Default implementation = user-mode high-level emulation (HLE) plus dynamic translation, not electrical/cycle-exact full-system emulation.

**Decomposed system:** guest application -> validated loader -> CPU interpreter/JIT + sparse guest memory -> scheduler/handle table/SVC/IPC/HLE services -> GPU frontend, shader IR, Vulkan device -> display, audio, input and sandbox storage -> desktop frontend. Each is testable without a game image.

**Explicitly out of scope for initial milestones:** booting Nintendo proprietary firmware; distributing firmware, ROMs, console keys, Nintendo assets or obtained secrets; defeating device security; Nintendo Network/online authentication; proprietary DLSS replication; bypass of DRM; assurances that any commercial game works.

**Definitions of done:**
- *Documentation done* = cited evidence with confidence and date, unresolved questions, reproducible original fixture.
- *Feature done* = unit+integration tests, boundary/fuzz cases, tracing, documentation, tested on primary host and CI green.
- *Subsystem done* = independently authored end-to-end guest test, cross-vendor or cross-platform smoke where appropriate, no critical known host safety defect.
- *Playable release* = real user-visible program execution verified with reproducible evidence; never infer from file detection or a program counter advancing.

### 1.1 Publicly reported hardware — evidence, not invented specification

| Element | Current public reporting | Confidence / caveat |
|---|---|---|
| SoC | Custom NVIDIA T239, identified also as GMLX30-A1 | Community PCB/research sourcing; validate device revisions |
| CPU | 8× 64-bit Cortex-A78C | SwitchBrew T239 research; exact software-exposed extensions require tests |
| GPU | Ampere-family, reported 1,536 CUDA cores | Core count is *not* a graphics command/ISA specification |
| Memory | 12 GB LPDDR5X, often two 6 GB packages | Physical capacity does not define a guest virtual-memory allocation |
| Storage | 256 GB UFS 3.1 modules reported | HLE can virtualize storage; no need to emulate NAND electronics |
| OS | Horizon family and a Switch 2-specific software stack | Versioned ABI differences have to be established, not guessed |
| Security | Public reports of PAC and memory-encryption capabilities | Do not turn research into bypass or key-extraction instructions |

Primary public sources: https://switchbrew.org/wiki/Switch_2:_Hardware ; https://switchbrew.org/wiki/Switch_2:_Tegra_T239 ; https://switchbrew.org/wiki/Main_Page . NVIDIA's product materials confirm a custom NVIDIA chip and new graphics features but not an entire GPU programmer manual. Vendor documentation for NVIDIA Orin/T234 is **analogy only**, not proof of T239 register equivalence.

### 1.2 Technology choices and decision points

| Domain | Initial choice | Why and qualification |
|---|---|---|
| Language | C++20 | Mature OS/JIT/Vulkan ecosystem and low-level control |
| Project/build | CMake + CTest; Ninja optional | First-party reproducibility and Windows/Linux tooling |
| CPU | Tiny reference interpreter -> vetted Dynarmic A64 JIT | Oracle first, speed later; pin a maintained 0BSD-compatible revision, audit feature gaps |
| Guest memory | Sparse page-backed address spaces | Fault handling/permissions without eagerly allocating 12 GB |
| Horizon | Modular HLE with service-version profiles | Correctness and independent service testing |
| Graphics | Vulkan + custom guest GPU frontend and shader IR | Explicit resource/synchronization control; separate host renderer from unknown guest commands |
| Shader tooling | SPIRV-Tools, glslang/shaderc as needed | Validate and optimize generated SPIR-V, do not mistake tools for guest ISA translators |
| Input/audio | SDL3 when dependencies introduced | Cross-platform input/audio; Joy-Con 2 hardware driver optional |
| Testing | CTest, doctest/Catch2 or GoogleTest at later phase, fuzz harnesses | Synthetic fixture provenance; deterministic runtime tests |
| CI | GitHub Actions Windows/Linux; sanitizers Linux | Zero-content builds and automatic regressions |
| Formatting | clang-format, clang-tidy, optional clangd | Predictable quality and developer workflow |
| Licensing | **Decision pending by repository owner** | Do not incorporate prior emulator GPL code or choose final project license silently |

**Do not initially copy an existing Switch emulator wholesale.** Use public architecture/specifications as reference, track licenses, and isolate borrowed third-party dependencies. Dynarmic: https://github.com/lioncash/dynarmic (archived historic repo; inspect a maintained fork and pin reviewed license/version); SDL: https://wiki.libsdl.org/SDL3/FrontPage ; Vulkan: https://docs.vulkan.org/guide/latest/ .

## 2. Phase-execution contract

Every phase below defines *Depends* (prerequisites), *Method* (what to implement and how) and *Exit gate* (observable proof). No phase is marked complete merely because a file or placeholder class exists. Parallelize independent research tracks B, GPU host proof-of-concept and HLE mocks, but merge only coherent tested changes on main. Use GitHub issues for phase tickets tagged foundation/research/cpu/kernel/gpu/peripheral/testing; attach source references, commits, logs, and reproduction steps. Revisit this plan when evidence disproves an assumption.

### Critical-path milestones

| Milestone | Required phases | Proof |
|---|---|---|
| M0 — Bootstrap | 00–06 | Buildable standalone native CLI/library, tests and source controls |
| M1 — CPU demo | 07–09, 16–20 | Original 7+35 AArch64 program returns 42 in interpreter and JIT |
| M2 — Guest process | 10–11, 21–27 | Authored ELF executes with memory, threads, SVC and IPC |
| M3 — HLE sandbox | 28–31 | Authored guest process can store/reload data and access services |
| M4 — Host graphics | 12, 32–34 | Host-owned Vulkan image/triangle, validated GPU memory/queue mock |
| M5 — Guest-rendered scene | 35–42 | **Verified** guest GPU dialect draws first pixel/triangle (synthetic IR can precede native T239 support) |
| M6 — Peripheral demo | 44–50, 53 | Authored interactive audio/graphics guest demo |
| M7 — Research compatibility | 43, 51–58 | Documented, safe, explicitly versioned Switch 2 behavior and tested limits |
| M8 — Long term | 59–60 | Reproducible public release, maintenance and evidence-driven additions |

**Parallel work:** research 07–15 informs all subsystems; memory/CPU 16–20 can run alongside Vulkan host bootstrap 32; SVC/IPC 25–31 needs loader/runtime; GPU guest translation 35–43 is blocked by 12 and 33–34. HLE first, proprietary firmware boot path deferred indefinitely unless independently justified and lawful.


## A — Charter, provenance, build and observable science

### Phase 00 — Scope, goals, and exclusions
**Depends:** None.
**Method / tooling:** Define Windows x64 first, Linux x64 second, ARM64 hosts later; user-mode HLE rather than cycle-accurate hardware simulation; guest-owned independent programs before application compatibility. Publish non-goals: no online Nintendo services, no firmware/key distribution, no promise of playable titles.
**Exit gate / artifact:** Approved scope, goal metric definitions, feature gates, and exclusions reviewed in docs/STATUS.md.

### Phase 01 — Clean-room and legal review
**Depends:** 00.
**Method / tooling:** Maintain a provenance log for every technical claim, artifact and contributor. Separate researchers writing behavioral specifications from implementers when working from independently observed behavior. Use lawfully acquired devices and content; prohibit copying code/assets, publishing keys, bypass instructions and protected dumps. Review jurisdiction-specific law before any test requiring circumvention.
**Exit gate / artifact:** docs/PROVENANCE.md and licensing policy exist; all sample data redistributable.

### Phase 02 — Repository architecture and ownership
**Depends:** 00.
**Method / tooling:** Create source folders for core, CPU, loader, memory, kernel, HLE, IPC, GPU, audio, input, VFS, frontend and tools. Define public interfaces, error model and directory conventions. Keep a single main branch with small auditable commits and reviewable changes.
**Exit gate / artifact:** Buildable root skeleton with app, library and test targets; documented architecture ownership.

### Phase 03 — C++20 build and reproducible toolchains
**Depends:** 02.
**Method / tooling:** Use CMake >=3.24, C++20, pinned dependencies and optional vcpkg manifest later. Windows MSVC/ClangCL and Linux Clang/GCC builds; forbid implicit downloads in default configure. Track exact compiler and SDK versions in build metadata.
**Exit gate / artifact:** Fresh checkout configures and builds without console artifacts on Windows and Linux CI.

### Phase 04 — Continuous integration and quality gates
**Depends:** 03.
**Method / tooling:** Enable warning levels, static formatting, unit tests, clang-tidy, sanitizer builds on supported hosts, dependency/license audits and artifacts. Require deterministic tests and pinned runners or version reports. Enforce coverage on changed critical runtime code; no skipping failing checks.
**Exit gate / artifact:** CI compiles and tests on Windows/Linux, captures logs and rejects regressions.

### Phase 05 — Observability and failure classification
**Depends:** 03.
**Method / tooling:** Build structured logging with subsystem/category, guest virtual time, thread ID, SVC ID, GPU queue ID and reproducibility hash. Add error taxonomy, optional trace capture, crash dumps without copyrighted buffers, performance counters, and test-replay harness.
**Exit gate / artifact:** A synthetic fault yields deterministic diagnostic output and reproducible trace ID.

### Phase 06 — Test corpus, oracle and evidence registry
**Depends:** 01,04.
**Method / tooling:** Create only original hand-authored AArch64 binaries, synthetic GPU shaders and host-side unit fixtures. Classify assertions as architectural, source-confirmed, experimentally observed or hypothesis. Record device model/OS version/tool/compiler for legal observations. Use Arm architecture reference, trusted CPU simulators and open GPU standards as partial independent oracles.
**Exit gate / artifact:** Every test has provenance, expected output and cross-check; hypothesis ledger is searchable.

## B — Hardware and software fact finding

### Phase 07 — SoC identification and revision matrix
**Depends:** 06.
**Method / tooling:** Build a sourced inventory: retail motherboard revisions, NVIDIA T239/GMLX30-A1, eight Cortex-A78C cores, Ampere GPU, LPDDR5X and related subsystems. Collect official specifications, accredited teardowns and SwitchBrew claims with confidence labels; do not assume T234/Orin registers equal T239.
**Exit gate / artifact:** Versioned hardware matrix with sources, uncertainties and alternate revisions.

### Phase 08 — CPU architectural capability study
**Depends:** 07.
**Method / tooling:** Map AArch64 ISA, Armv8.x extension set, pointer authentication, atomics, SIMD/FP, exclusive monitors, exceptions and observed feature registers only where lawfully measurable. Identify gaps in selected recompiler; write original tests for each extension before supporting it.
**Exit gate / artifact:** CPU capability matrix links each supported instruction family to passing tests.

### Phase 09 — Memory/cache/MMU study
**Depends:** 07.
**Method / tooling:** Research user virtual-address space, page granules, layout randomization, executable permissions, cache-maintenance requirements, shared memory and memory ordering. Distinguish CPU architectural requirements from unverified Switch 2 particulars; emulate user-visible effects, not DRAM electronics.
**Exit gate / artifact:** Documented memory contract and fault semantics; unknowns isolated behind parameters.

### Phase 10 — Horizon kernel and userspace ABI dossier
**Depends:** 06,07.
**Method / tooling:** Survey public SwitchBrew kernel/SVC/IPC docs and open-source userland headers as historical starting points. Create service-ID, SVC, handle and layout inventories; annotate Switch 1 versus Switch 2 uncertain differences; independently validate with permitted behavior tests.
**Exit gate / artifact:** Version-tagged ABI tables, unknown markers, test priority ranking.

### Phase 11 — Executable and metadata format dossier
**Depends:** 06,10.
**Method / tooling:** Document known and suspected executable container structures, relocation formats, manifests and title metadata. Start with ELF64 produced by own compiler and add only lawfully documented/decrypted independently created formats. Make content acquisition an isolated, explicit user responsibility.
**Exit gate / artifact:** Parser specs, malformed-input fixtures, zero dependence on proprietary content.

### Phase 12 — GPU architecture and driver-interface dossier
**Depends:** 07.
**Method / tooling:** Study public Ampere literature, T234/Orin public manuals and open-source NVIDIA drivers as analogy only. Inventory command buffers, GPU virtual addresses, queue submissions, shader machine code, texture tiling, compute and display. Mark every guessed packet/register/ISA encoding as unverified until observed.
**Exit gate / artifact:** GPU unknowns map with numbered hypotheses and reproduction plans, not a fabricated complete specification.

### Phase 13 — Controller, display, audio and storage dossier
**Depends:** 07.
**Method / tooling:** Inventory Joy-Con 2, USB/Bluetooth HID surfaces when publicly documented, handheld/docked display timings, audio stream characteristics, save-data semantics and storage classes. Favor passive HDMI/USB enumeration and user-authorized supported interfaces; avoid protection-sensitive protocol probing.
**Exit gate / artifact:** Peripheral capability sheets with observational provenance and test limits.

### Phase 14 — Measurement apparatus and hypothesis discipline
**Depends:** 07-13.
**Method / tooling:** Create benchmark clocks, trace formats, HDMI capture protocols, repeatability criteria and device safety checklists. For lawful black-box trials vary exactly one setting per run, control temperature/power state, collect multiple samples, compute variation, log null results. No invasive voltage glitching, protected-memory acquisition or exploit dependency.
**Exit gate / artifact:** Published research protocol and validated reusable measurement templates.

### Phase 15 — Switch-generation compatibility differential
**Depends:** 10,12.
**Method / tooling:** Construct difference tables for Switch 1 vs Switch 2 Horizon services, memory, execution, display and graphics. Treat apparent compatibility as a research hypothesis; do not copy prior emulator internals blindly. Design versioned compatibility shims.
**Exit gate / artifact:** Per-feature compatibility classifications: common, version-specific, unresolved, or out of scope.

## C — Foundation runtime, CPU and loader

### Phase 16 — Core abstractions and lifecycle
**Depends:** 02,05.
**Method / tooling:** Define EmulatorSession, GuestProcess, AddressSpace, CpuContext, Scheduler, KernelObject, ServiceManager, GpuDevice, AudioDevice and HostPlatform interfaces; establish RAII, ownership/lifetimes and dependency injection. Decouple CPU and Vulkan from platform shell.
**Exit gate / artifact:** Mock backend session constructs/destroys safely and emits a session trace.

### Phase 17 — Sparse virtual address space
**Depends:** 09,16.
**Method / tooling:** Implement page-mapped virtual memory with lazy physical backing, checked arithmetic, R/W/X permissions, guard pages, mapping provenance, aliasing and coherent shared buffers. Never eagerly allocate a literal 12 GB just because physical console RAM is 12 GB. Protect host against malicious guest addresses.
**Exit gate / artifact:** Boundary tests for wraparound, cross-page operations, map/unmap, alias, permissions and out-of-memory.

### Phase 18 — Reference AArch64 interpreter
**Depends:** 08,17.
**Method / tooling:** Write a deliberately small correct interpreter for A64 integer data-processing, branches, loads/stores, BRK and basic exceptions, using original instruction fixtures. Keep it as correctness oracle and deterministic fallback rather than a full production CPU.
**Exit gate / artifact:** Runs hand-authored 7+35=42 test and randomized arithmetic/control-flow vectors.

### Phase 19 — Dynarmic JIT integration
**Depends:** 17,18.
**Method / tooling:** Evaluate maintained Dynarmic forks, pin revision and exact 0BSD/license obligations. Implement memory callbacks, code cache invalidation, exception callbacks, SVC/BRK routing, exclusive monitor configuration and cache protection. Retain interpreter fallback for unsupported instructions.
**Exit gate / artifact:** Interpreter and JIT agree on register/memory outcomes for baseline corpora.

### Phase 20 — ARM feature and precision completion
**Depends:** 08,19.
**Method / tooling:** Cover FP/NEON rounding, atomics, barriers, PAC-related guest-visible behavior if present, system register reads, thread-local registers, scheduling points and invalid instruction handling. Never equate Dynarmic's broad ARMv8 support with measured Cortex-A78C parity.
**Exit gate / artifact:** ISA matrix passes original differential tests, with unsupported instructions reported explicitly.

### Phase 21 — Guest scheduler and synchronization primitives
**Depends:** 17,19.
**Method / tooling:** Model guest threads, priorities, wait queues, mutexes, events, timers, atomics and virtual clock. Start deterministic single-thread stepping; later coordinate JIT CPU workers while preserving guest-visible ordering. Avoid directly mapping every guest priority to OS scheduler.
**Exit gate / artifact:** Repeatable multithread tests prove no lost wakeups, atomicity regressions or deadlocks.

### Phase 22 — ELF64 synthetic loader and relocation
**Depends:** 11,17.
**Method / tooling:** Implement bounds-checked ELF64/AArch64 PT_LOAD reader, section/program header validation, relocations needed for independently built test payloads, stack/auxiliary data, TLS and runtime entry. Fuzz all length fields and reject malformed files.
**Exit gate / artifact:** Original AArch64 ELF launches, prints a synthetic SVC message and exits deterministically.

### Phase 23 — Runtime linker and module lifecycle
**Depends:** 22.
**Method / tooling:** Introduce module map, symbol resolution, import/export stubs, TLS setup, library lifetime and safe unloading. Keep Switch-specific NSO/NRO support behind separate proven-format adapters; never treat encrypted game containers as ordinary ELF.
**Exit gate / artifact:** Two authored modules resolve imports and clean up with ASLR enabled.

### Phase 24 — CPU/runtime diagnostic debugger
**Depends:** 18-23.
**Method / tooling:** Add guest register viewer, reversible-friendly state snapshots, instruction disassembly, breakpoints, watchpoints and tracing filters. Support address-space mapping view and deterministic short replays without recording proprietary user content.
**Exit gate / artifact:** Scripted debugger finds an intentional guest memory fault reproducibly.

## D — Horizon-style high-level operating system

### Phase 25 — Handles, objects and process model
**Depends:** 10,16,17.
**Method / tooling:** Create per-process handle tables, kernel object types, rights, lifetime/reference semantics, process/thread IDs and error returns. Verify public ABI assumptions; avoid hardcoding Switch 1 values into immutable APIs.
**Exit gate / artifact:** Unit tests for stale handles, invalid rights, duplication, limits and teardown.

### Phase 26 — System-call dispatch
**Depends:** 20-22,25.
**Method / tooling:** Implement versioned SVC dispatcher and parameter marshalling (general registers, return values, pointer validation, faults). Prioritize threading, memory, timing, events, address-space and IPC calls exercised by authored test clients.
**Exit gate / artifact:** Each implemented SVC has normative/empirical reference, negative tests and trace signatures.

### Phase 27 — IPC transport and service manager
**Depends:** 25,26.
**Method / tooling:** Implement domains, sessions, command headers, transfer buffers, copy/move handles, handle counting, timeouts and close semantics. Maintain decode/encode golden vectors, fuzz all lengths and ensure no host pointer escapes.
**Exit gate / artifact:** Authored client/server exchanges messages, handles malformed packets safely.

### Phase 28 — Filesystem and save-data HLE
**Depends:** 23,27.
**Method / tooling:** Implement read-only virtual executable FS, sandboxed per-title writable save storage, mount points, file handles, directory enumeration, atomic writes and path normalization. Prohibit traversal and host file leakage; no Nintendo partitions required.
**Exit gate / artifact:** Guest tests create/reopen/save/recover data and cannot access host paths outside sandbox.

### Phase 29 — Time, settings and lifecycle services
**Depends:** 21,27.
**Method / tooling:** Implement monotonic/wall clocks, event scheduling, language/region settings, launch/shutdown state, application events and requested performance profiles with controllable fake data. Version all platform constants.
**Exit gate / artifact:** A synthetic application boots, reads settings, sleeps, resumes and exits.

### Phase 30 — Input, applet and UI services
**Depends:** 27,29.
**Method / tooling:** Implement basic HID abstraction, button mapping, touch and frontend focus, then minimum applet services used by independently created demos. Make full retail UI emulation optional and explicitly unimplemented initially.
**Exit gate / artifact:** Synthetic gamepad client observes press/release and applet lifecycle events.

### Phase 31 — Version-specific Switch 2 service layer
**Depends:** 10,15,25-30.
**Method / tooling:** Fork service implementations only where documented behavior differs; use versioned factories and feature flags rather than brittle string checks. Add regression cases per OS version when evidence exists; maintain unknown/unimplemented behavior list.
**Exit gate / artifact:** Service tests can run across explicitly selected compatibility profiles without silent fallback.

## E — NVIDIA GPU research and Vulkan rendering

### Phase 32 — Host Vulkan bootstrap
**Depends:** 03,16.
**Method / tooling:** Add optional Vulkan dependency, device selection, validation layers, surface/swapchain lifecycle and headless offscreen renderer. First render a host-owned triangle before accepting any GPU guest command.
**Exit gate / artifact:** Reference triangle renders on NVIDIA/AMD/Intel host where Vulkan features suffice; validation reports no errors.

### Phase 33 — GPU address space and memory coherency
**Depends:** 09,12,17,32.
**Method / tooling:** Model guest GPU VA allocation, CPU/GPU address translation, mapped buffer lifetime, caches and resource dirty tracking. Verify unknown page sizes empirically before treating them as fixed; separate conceptual device mappings from host Vulkan allocations.
**Exit gate / artifact:** Synthetic buffer upload/download and invalid mapping tests have exact byte agreement.

### Phase 34 — GPU queue, FIFO and synchronization
**Depends:** 12,21,33.
**Method / tooling:** Define pushbuffer/command queue abstraction, fences, semaphores, timelines, interrupts and scheduler integration. Build an internal documented synthetic packet dialect initially; do not mislabel it as native T239 methods.
**Exit gate / artifact:** Cross-queue scripted workload runs deterministically; sync races caught in tests.

### Phase 35 — Guest GPU command decoding research
**Depends:** 12,14,34.
**Method / tooling:** Translate verified command structures into typed GPU IR; document each field with evidence confidence and minimal reproducible probes. Maintain generation-specific decoders. Unknown commands log safely without pretending to render.
**Exit gate / artifact:** At least one independently specified set of draw/clear/copy semantics passes round-trip packet tests.

### Phase 36 — Shader container parsing and shader ISA dossier
**Depends:** 12,35.
**Method / tooling:** Study public NVIDIA graphics ISA references and legal shader examples; distinguish CUDA SM numbering from a verified graphics program encoding. Use separate validation stage for binary structure, operands, resource binding and control flow. Reject unsupported opcodes.
**Exit gate / artifact:** Parser handles original synthetic fixtures, with bounded reads and IR validation.

### Phase 37 — Shader intermediate representation
**Depends:** 36.
**Method / tooling:** Design typed SSA-like or structured IR for control flow, integer/FP vectors, textures, descriptor bindings, shared memory and barriers. Track precision/rounding requirements and shader stage. Include constant folding, dead code and diagnostics.
**Exit gate / artifact:** Independent shader IR tests for branching, loops, type legality and optimization equivalence.

### Phase 38 — Shader to SPIR-V compilation
**Depends:** 32,37.
**Method / tooling:** Emit Vulkan-compatible SPIR-V using a pinned generator or custom emitter; run SPIRV-Tools validator and Vulkan validation on each shader. Add disk cache keyed by shader hash, host capabilities and compiler revision, and diagnostic opt-out.
**Exit gate / artifact:** Original synthetic vertex/fragment shaders translate, validate and draw expected pixel patterns.

### Phase 39 — Formats, tiling, textures and samplers
**Depends:** 33,35,38.
**Method / tooling:** Model linear/block-linear layout hypotheses, bytes-per-pixel, tiling transforms, compression formats, swizzles, mipmaps, sampling, LOD, depth and stencil. Implement compute-side reference conversions and property tests before GPU acceleration.
**Exit gate / artifact:** Golden texture patterns, corner cases and format conversion checksums match reference.

### Phase 40 — Raster pipeline, state translation and blending
**Depends:** 35,38,39.
**Method / tooling:** Translate vertex layout, instancing, primitive topology, clipping, viewport, scissor, multisample, blend equations, depth/stencil and render target lifecycle. Cache pipelines; track invalidation from guest state changes.
**Exit gate / artifact:** Deterministic synthetic golden-image suite across multiple Vulkan vendors.

### Phase 41 — Compute and memory barriers
**Depends:** 34,37-40.
**Method / tooling:** Add compute dispatch, workgroup shared memory, storage images, atomics and guest/host synchronization semantics; ensure command scheduling and queue ownership transitions are legal Vulkan.
**Exit gate / artifact:** Original compute kernels generate known buffers/images, under GPU validation.

### Phase 42 — Presentation and display modes
**Depends:** 13,32,40.
**Method / tooling:** Implement handheld/docked logical rendering settings, scaling, aspect-ratio policy, orientation, variable refresh assumptions only where observed, frame pacing and vsync. Keep optional upscalers separate; do not claim proprietary DLSS duplication.
**Exit gate / artifact:** End-to-end guest-rendered synthetic moving scene presents at stable cadence and correct dimensions.

### Phase 43 — Graphics compatibility expansion
**Depends:** 12,35-42.
**Method / tooling:** Prioritize missing verified shader instructions, geometry/tessellation stages, advanced attachments, texture formats, shader precision and GPU faults by test impact. Build per-feature implementation contracts and avoid one-off title hacks.
**Exit gate / artifact:** GPU feature matrix and regression gallery improve monotonically without widespread regressions.

## F — Peripheral HLE, host UI and operational robustness

### Phase 44 — Host input and Joy-Con abstraction
**Depends:** 13,30.
**Method / tooling:** Start SDL3 standard gamepad/keyboard, hot-plugging, remapping and deadzones. Treat physical Joy-Con 2 communication as a separate optional research driver relying on lawful open protocols and user consent.
**Exit gate / artifact:** Controllers can connect/disconnect mid-session without crashing, with synthetic HID timing tests.

### Phase 45 — Audio core and synchronization
**Depends:** 13,21,27.
**Method / tooling:** Build mixer, output device, resampling, channel layouts, volume and event scheduling; integrate SDL3 audio or an explicitly reviewed backend. Compare frames to monotonic guest timeline; track buffering/underrun.
**Exit gate / artifact:** Authored sine-wave app plays at expected pitch; latency and drift measured.

### Phase 46 — Network and local communication stubs
**Depends:** 27,29.
**Method / tooling:** Implement safe deterministic offline network responses, local sockets for authored test clients when needed, and service errors. Do not emulate Nintendo authentication, account services, matchmaking or bypass access controls.
**Exit gate / artifact:** Offline synthetic client behaves predictably and cannot contact Nintendo services implicitly.

### Phase 47 — Storage, save states and portability
**Depends:** 17,21,28,34,45.
**Method / tooling:** Version snapshot state for CPU/kernel/memory/audio/GPU metadata; validate internal consistency; implement crash-safe save data and optional snapshots. Make GPU resource reconstruction explicit; do not save host pointers.
**Exit gate / artifact:** Versioned save-state compatibility test restarts deterministic synthetic programs.

### Phase 48 — Frontend, settings and accessibility
**Depends:** 16,29,42,44,45.
**Method / tooling:** Create minimal desktop GUI (SDL3 initially; optional Qt after an ADR), file picker limited to supported authored programs, logs/status views, key mapping, graphics config and accessible controls. Clearly label unsupported Switch 2 title loading.
**Exit gate / artifact:** User can run and stop synthetic demo, inspect warnings and export a redacted diagnostic bundle.

### Phase 49 — Hardware abstraction and portability
**Depends:** 19,32,44,48.
**Method / tooling:** Separate Windows/Linux host services, file paths, memory permissions, JIT W^X, high-resolution clocks and GPU capabilities. Add fallback behavior and informative errors for unsupported features.
**Exit gate / artifact:** Windows and Linux both execute original programs with same deterministic trace.

### Phase 50 — Timing, efficiency and thermal behavior
**Depends:** 07,14,21,42.
**Method / tooling:** Benchmark CPU instruction throughput, emulated guest sleep/timer variance, Vulkan upload cost, frame pacing, caching and host utilization. Model power/performance profiles only from evidence; never claim voltage-level fidelity.
**Exit gate / artifact:** CI/perf dashboard reports repeatable baselines; no unjustified 'full-speed' claim.

## G — Security, compatibility milestones and shipping

### Phase 51 — Fuzz hostile inputs and security boundaries
**Depends:** 11,17,22,27,35,36,39.
**Method / tooling:** Fuzz ELF, IPC, GPU packets, shader decoding, VFS paths, save states and front-end metadata. Apply address/memory/undefined-behavior sanitizers, timeouts, resource limits and crash minimization.
**Exit gate / artifact:** Seed corpus reproducibly covers parser gates; no known critical host escape or memory safety issues.

### Phase 52 — Differential accuracy lab
**Depends:** 06,18-50.
**Method / tooling:** Use Arm ISA architectural tests, own source-program oracles, synthetic graphics golden frames, audio checksums and portable HLE mocks. Where a lawful Switch 2 oracle is unavailable, record the gap rather than assert parity.
**Exit gate / artifact:** Each major feature has oracle class, test result, provenance and known limits.

### Phase 53 — Original AArch64 console-style demo suite
**Depends:** 22,25-31,40-45.
**Method / tooling:** Write first-party AArch64 test apps for memory/threading, IPC, text overlay, color triangle, gamepad, audio and save data. Provide source, scripts and redistributable assets. No retail title dependencies.
**Exit gate / artifact:** At least 6 independently authored guest demos execute through the public launcher.

### Phase 54 — Native Switch 2 functionality research gate
**Depends:** 15,31,43,53.
**Method / tooling:** Only after sufficient public/legal evidence exists, evaluate unimplemented native APIs/formats; verify OS version and platform state through authorized observations. Don't use encrypted retail images or undocumented decryption in public test suite.
**Exit gate / artifact:** Explicit 'supported/partial/unsupported' gate review with reproducible evidence.

### Phase 55 — Switch 1 compatibility-profile experiments
**Depends:** 15,31,53.
**Method / tooling:** Optional: add separate previous-generation compatibility profile based on independently validated interface changes; manage distinct GPU and kernel behavior. Never assume Switch 1 success means native Switch 2 support.
**Exit gate / artifact:** Per-profile test reports and feature-scoped fallback toggles.

### Phase 56 — Early-alpha quality milestone
**Depends:** 04,24,48,51-54.
**Method / tooling:** Deliver signed/checksummed dev builds when possible, onboarding guide, supported host GPU list, crash-report opt-in and repeatable demos. Note experimental status prominently.
**Exit gate / artifact:** One-click-ish install and launch for synthetic demos; documented non-goals and no proprietary payloads.

### Phase 57 — Performance engineering and cross-vendor GPU tests
**Depends:** 43,49,50,56.
**Method / tooling:** Profile JIT block cache, address translation, IPC overhead, shader cache, pipeline creation and frame spikes using reproducible benchmarks. Compare AMD/NVIDIA/Intel without assuming a single vendor is representative.
**Exit gate / artifact:** Published transparent host/device matrix, percentile latency and regression budgets.

### Phase 58 — Broader lawful application compatibility research
**Depends:** 54,56,57.
**Method / tooling:** If legally permissible independently acquired binaries become testable, keep test material outside repo; record authorized title/version, known behavior and failure reproduction without sharing protected content. Prioritize generic compatibility fixes.
**Exit gate / artifact:** Redacted compatibility database with reproducible cases and no keys/content.

### Phase 59 — Public beta, release and maintenance
**Depends:** 51,56-58.
**Method / tooling:** Release versioned binaries/source with SBOM, dependency notices, vulnerability intake, issue templates, diagnostics and documented limitations; rotate maintainers, plan API stability and regression triage.
**Exit gate / artifact:** Reproducible release artifacts, policy compliance, stable regression suite and honest compatibility reporting.

### Phase 60 — Long-term research backlog
**Depends:** 07-15,59.
**Method / tooling:** Track still-unknown T239 GPU methods, hardware revisions, system-version deltas, shader edge cases, controller modes, timing and alternative host GPU backends; schedule each as an evidence-backed project rather than vague 'complete Switch 2' task.
**Exit gate / artifact:** Prioritized backlog with owner, evidence, measurable success criteria and explicit blocked reasons.


## 3. How hardware reverse engineering will actually work

The core loop is **public documentation -> question -> falsifiable hypothesis -> lawful observable experiment -> repeat with controls -> minimal behavioral contract -> implementation -> differential regression**. Research does *not* begin by guessing inaccessible MMIO registers.

### 3.1 Observation streams (in priority order)

1. **Documentary:** Nintendo/NVIDIA public product statements, Arm ISA manuals, SwitchBrew source history, PCB teardowns, board-part identification, public vendor/Orin documents and Linux/open NVIDIA drivers. Every reused fact gets URL, date, revision and confidence.
2. **Architectural simulation:** Original AArch64 assembly compiled with Clang/LLVM, architectural interpretation, instruction encoding checks, differential comparison against an independent ARM64 reference environment. A generic ARM board is an ISA oracle, **not** a Switch 2-specific HLE oracle.
3. **Passive functional hardware tests:** With a lawfully owned, unmodified console, record ordinary user-visible changes: HDMI output frame timings/modes, input latency, dock/handheld transitions, acoustic output, officially exposed system-information screens. Keep all experiments within device terms and applicable law.
4. **Safe interface enumeration:** Where exposed through documented host interfaces, record USB descriptors or standard HID event timing from user-owned devices; never probe restricted services or require an exploit. Unknown wireless/controller protocols remain an optional blocked research task.
5. **Independent original test programs:** When there is a legitimate approved route to execute original code on comparable hardware or Switch 2 itself, run minimal deterministic user-space CPU/API/graphics tests and collect *behavioral observations only*. **Do not assume such an execution route exists today.**
6. **Source/driver analogies:** Public NVIDIA Ampere/T234/Open GPU driver details can suggest hypotheses for queues, tiling and synchronization. Mark T239 inheritance as *unconfirmed* until corroborated.
7. **Test-driven specification:** Convert verified observations into pure data tables, diagrams, readable pseudocode and test vectors. Reconstruct functional behavior, not proprietary source.

### 3.2 Specific research dossiers / protocols

- **CPU:** extension bits/ID registers if legitimately available; instruction behavior, PAC exposed semantics, barriers, exclusive monitors and per-thread registers. Implement ISA-required semantics even without Switch-specific device access. Log confidence.
- **Memory:** observe only legally exposed mapping behavior; establish minimum address ranges, alignment, permissions, aliases, executable transitions, faults, heap and TLS, using original fixtures and safe system APIs.
- **Kernel/HLE:** maintain SVC-by-SVC ABI sheets: identifier, parameter registers, output/error contracts, object rights, pointer validation, OS-version provenance, tests. For IPC record packet schemas, domains, buffer direction, service lifecycle; redact captures.
- **GPU frontend:** develop a *hypothesis-to-command* corpus from public documents and independently authored shader/draw tests where lawful execution is available; start with abstract internal command IR before native queue-decoder implementation. Test clear, copy, triangle, texture, compute, barrier in that order. Keep every undocumented opcode behind a feature gate.
- **Shader ISA:** differentiate public Ampere compute/CUDA concepts from actual graphics binary encodings. Build encoder-independent synthetic SSA IR, GLSL/SPIR-V reference programs and proof traces. Never claim proprietary binary translation until real test vectors and semantics are validated.
- **Textures:** test row pitch, block compression, swizzle, format, mip/sampler and depth edge cases via original images and CPU reference converters.
- **Presentation:** ordinary HDMI capture and display settings are observable without defeating console security; correlate guest virtual ticks with captured wall clock under controlled display/thermal settings.
- **Audio/input:** compare independently generated tones, HID events, rate conversion, hot-plug, sample drift; capture no personal or protected content.
- **Storage:** emulate the *application-facing* filesystem contract with host sandbox; physically reverse engineering UFS is unnecessary to HLE.
- **Versions:** keep OS/hardware revision fields in evidence and compatibility reports; never mix findings from different firmware builds without marking discrepancy.
- **Missing evidence:** if no lawful original-code execution or traces exist, mark the native GPU/HLE feature **BLOCKED** and proceed with independent CPU, host graphics and abstract services. Do not bridge missing information with made-up implementations.

### 3.3 Evidence card template

Store each research note under docs/research/ as Markdown:
- ID / title / subsystem; *Claim* and *observable result* separately.
- Source URLs with access date; device revision and OS version if applicable.
- License/provenance and whether redistributable; researcher's name or role.
- Hypothesis and rival explanations; exact original fixture and commands; environment.
- Controls, number of runs, output summary, variation/uncertainty and failure cases.
- Confidence: **CONFIRMED** (repeatable original test/documented contract); **REPORTED** (public source not independently validated); **INFERRED** (cross-generation analogy); **UNKNOWN/BLOCKED**.
- Consequence for code; linked PR/commit/test; retest trigger.

### 3.4 Instrument kit (approved, ordinary observation)

Developer workstation, Windows/Linux, C++20/LLVM cross-toolchain, oscilloscope only for **accessible unprotected signals** where safe, HDMI capture interface, ordinary USB protocol analyzer on supported host-side public links, gamepad tester, optional power meter, temperature logging and a purchased console. The oscilloscope or analyzer is **not a prerequisite** to software HLE. No chip decapping, fault injection, firmware extraction, flash dumping, key derivation or console security bypass belongs in this project plan.

## 4. Initial repo structure and implementation units

~~~text
Emulation-Nint2/
  README.md                        # onboarding; accurate current status
  AGENTS.md                        # development execution rules
  CMakeLists.txt                   # C++20 build entry
  .github/workflows/ci.yml         # Windows/Linux tests
  include/nint2/
    memory.hpp                     # small validated proof-of-concept interface
  src/app/main.cpp                 # CLI smoke entrypoint
  src/core/memory.cpp              # initial mapped guest memory
  tests/memory_tests.cpp           # deterministic smoke/permission tests
  docs/
    MASTER_DEVELOPMENT_PLAN.md     # THIS roadmap
    HARDWARE_RESEARCH.md           # field protocol and unknowns
    ARCHITECTURE.md                # module contracts and ADRs
    STATUS.md                      # actual implemented vs planned
    research/                      # future evidence cards
  (future) src/cpu/, src/loader/, src/kernel/, src/hle/,
           src/gpu/, src/audio/, src/input/, src/vfs/, src/frontend/,
           tools/, tests/corpus/, third_party/
~~~

**Layer contracts:** CPU interacts with an abstract GuestAddressSpace and exception callback, never with Vulkan; HLE validates pointers and wraps kernel objects; GPU frontend produces guest-agnostic render IR and owns GPU VA mappings; Vulkan backend consumes validated IR; frontend owns host input/output but no kernel state. Cross-boundary communication uses bounded, versioned data types, not void* into guest buffers. CI remains compilable as modules are introduced.

### Planned CLI evolution

- Stage M0: nint2 --help; nint2 --version; nint2 --smoke (native memory self-test, **not** guest execution).
- Stage M1: nint2 --run-a64 tests/fixtures/add42.bin --cpu=interp|jit.
- Stage M2: nint2 --run-elf tests/guest/<original>.elf --trace=svc.
- Stage M5: nint2 --run-guest tests/guest/triangle.elf --gpu=vulkan --headless.
- Stage M7+: versioned launcher/GUI and compatibility reporting, no promise of commercial content.

## 5. Quality gates, test design and measurable SLOs

**Security:** all guest pointers range checked; all parsers length bounded; host file access sandboxed; guest data is untrusted; release never contains proprietary Nintendo artifacts. Use parser fuzzing and ASan/UBSan in supported CI. Avoid dynamic host code execution except vetted JIT pages with host W^X policies.

**Determinism:** seedable RNG and virtual monotonic time; replay scheduler for scripted fixtures; store host GPU/driver versions, instruction/JIT revision and pipeline hashes; golden image comparisons permit documented tolerances when GPU formats vary.

**Performance (targets, not achieved claims):** regressions defined as >10% median CPU ops/s or >10% p95 frame time on pinned bench host after 10-run comparison; 99th percentile frame time and shader compile spikes measured separately. No 'full-speed' claim without title/device/host-specific evidence. CPU JIT overhead and GPU driver overhead have independent budgets.

**Correctness:** instruction corpus covers flags, branch, load/store, alignment, atomics, NEON, FP, faults and unsupported instruction behavior as availability allows. Kernel tests cover invalid handles, zero length/overflow, thread lifecycle and denied rights. Graphics image suite includes triangles, clipping, texture mips, barriers, compute, depth and blending.

**CI release policy:** no new feature passes without automated evidence; issues require reproducer+expected/actual; regression tests pin prior bugs. Public compatibility claims must show exact program, legally obtained test status, host specs, relevant OS/profile version and date.

## 6. Risk register and decision rules

| Risk | Probability | Consequence | Response |
|---|---|---|---|
| Native T239 GPU command/graphics ISA unavailable | Very high | Blocks native graphics | Build abstract IR / Vulkan synthetic pipeline; publish BLOCKED gate |
| Horizon service/version mismatch | High | Guest startup fails | Versioned ABI registry; evidence-based contracts |
| Partial ARMv8.x feature support in chosen JIT | High | Guest illegal instruction/miscompare | Interpreter oracle; capability audit; fallback |
| Host GPU Vulkan feature disparity | High | Render mismatch | Feature negotiation, fallback, multi-vendor golden tests |
| Native app formats depend on protected content | High | No legal distributable corpus | Original test ELF and first-party assets exclusively |
| License conflicts from reusing old emulator code | High | Release blocked | Third-party review + owner license decision before import |
| Performance premature optimization | High | Hard-to-debug bugs | Deterministic correctness baseline before micro-optimizing |
| Driver/OS changes | Medium | Regressions | Environment capture, continuous CI and pinned baselines |
| Hardware access not available | High | Unknown Switch2-specific behavior | Mark blocked; don't infer device semantics as established |
| Developer burnout / scope creep | High | Unfinished project | Phase gates, small standalone wins, triaged backlog |

**Go/no-go policy:** if sources or test pathways cannot validate a native behavior, stop that research implementation at an abstract stub; do not synthesize fake registers or claim game compatibility. Work that is still useful continues on CPU, tests, HLE architecture and host Vulkan.

## 7. The first ten executable work items

1. Commit this plan and clean-room policy; confirm owner wants an explicit OSS license (or keep redistribution policy unresolved).
2. Establish CMake, CLI, core test and Windows/Linux CI; document actual pass/fail in STATUS.
3. Introduce sparse memory API, permissions, bounds and tests; ensure 64-bit integer overflow checks.
4. Build a tiny AArch64 reference interpreter capable of add42 from original A64 machine code.
5. Compile and run own AArch64 ELF64 test payload with Clang cross target.
6. Integrate a **pinned** Dynarmic AArch64 backend after license/architecture review.
7. Implement thread-local state, instruction exceptions and debugger registers.
8. Implement basic guest process handle/SVC tables with original test programs.
9. Bring up Vulkan in a completely independent host-owned triangle test.
10. Open evidence tickets for CPU extension gaps, Horizon version differences and T239 GPU queue/shader unknowns.

## 8. External reference index (reference, not copied code)

- SwitchBrew landing and service docs: https://switchbrew.org/wiki/Main_Page
- Switch 2 hardware inventory: https://switchbrew.org/wiki/Switch_2:_Hardware
- T239 community research: https://switchbrew.org/wiki/Switch_2:_Tegra_T239
- SwitchBrew Switch 2 compatibility: https://switchbrew.org/wiki/Switch_2:_Compatibility_Mode
- Original Switch libnx headers (prior-generation ABI context): https://github.com/switchbrew/libnx
- ARM architecture documentation: https://developer.arm.com/architectures/cpu-architecture/a-profile
- Dynarmic historical reference (archived, audit active alternatives): https://github.com/lioncash/dynarmic
- Vulkan developer guide: https://docs.vulkan.org/guide/latest/
- Vulkan validation: https://github.com/KhronosGroup/Vulkan-ValidationLayers
- Khronos SPIRV-Tools: https://github.com/KhronosGroup/SPIRV-Tools
- Khronos SPIRV-Cross (for reflection/language targets, not NVIDIA decoding): https://github.com/KhronosGroup/SPIRV-Cross
- SDL3 documentation: https://wiki.libsdl.org/SDL3/FrontPage
- Existing early-stage Switch 2 research emulators (inspiration only, audit license): https://github.com/pragma-emu/pragma and https://github.com/pound-emu/pound

**Evidence date:** 2026-10-21. These references change. Every implementation issue must recheck current technical claims and the license of the *specific revision* imported.

## 9. Reality checks

There are no justified fixed delivery dates for native Switch 2 games. This is a complex multi-year, multidisciplinary reverse-engineering undertaking; core bottlenecks are lawful access to independent test oracles, GPU protocol/ISA documentation, Horizon differences and accurate shader translation. A well-engineered original-code runner can be useful long before console game compatibility. The plan must preserve that distinction at every release.
