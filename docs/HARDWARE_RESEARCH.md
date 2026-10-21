# Nintendo Switch 2 Hardware Reverse-Engineering Program

**Created:** 2026-10-21. Companion to [the master roadmap](MASTER_DEVELOPMENT_PLAN.md), especially phases 07–15 and 32–43.

## Goal and non-negotiable scope

Investigate **application-observable behavior**, not a transistor-perfect T239 model or secure-boot bypass. Research must use public documentation, original programs, normal user-visible outputs from lawfully owned equipment and other authorized observational work. Never commit copyrighted games/firmware, console keys, protected captures, confidential SDKs or exploit instructions. If an experiment depends on privileged access we do not legitimately have, mark it **BLOCKED** and continue the independent CPU, HLE mock and Vulkan research tracks.

## Baseline information and unknowns

| System | Publicly reported facts | Critical questions |
|---|---|---|
| SoC | NVIDIA T239 (GMLX30-A1) | Chip/board revisions, observable device configuration |
| CPU | Eight Arm Cortex-A78C cores | Guest-visible ARMv8.x features, atomics, PAC/system register semantics |
| GPU | Ampere-family, 1,536 CUDA cores reported | Real graphics command methods, GPU memory mapping, shader binary format |
| RAM | 12 GB LPDDR5X | Guest address layout, aliases, permissions, cache coherence |
| Kernel | Horizon family | Native Switch 2 ABI, service versions and SVC/IPC differences |
| Storage | UFS 3.1 | Filesystem and save behavior, not physical flash emulation |
| Display | Handheld and docked output | Frame scheduling, scaling and user-visible timing |
| Controls/audio | Joy-Con 2, touchscreen, audio | Guest service layout, timing and host integration |

Sources: https://switchbrew.org/wiki/Switch_2:_Hardware and https://switchbrew.org/wiki/Switch_2:_Tegra_T239 . These are valuable community findings, not a complete NVIDIA GPU or Switch 2 firmware programming manual. The T234/Orin platform is a *possible analogy only*. Any raw packet, MMIO offset or shader encoding inferred from it remains **INFERRED**, not CONFIRMED.

## Research loop

For every unclear feature:

1. Write a falsifiable question and two competing behavioral hypotheses.
2. Collect public source URLs, exact revisions, retrieval dates and licenses.
3. Design a minimum *original* test case that distinguishes the hypotheses.
4. Choose a permitted oracle: Arm architecture specs, independent CPU implementation, original mock services, ordinary device-output observations, or legally authorized original-code execution if one becomes available.
5. Control hardware revision, software version, host, compiler, input, timing, environment and run count; measure repeatability.
6. Record observed outputs, uncertainty, limitations and a sanitized fixture hash.
7. Turn only validated behavior into a typed emulator contract; reject unknown opcodes and unsupported services.
8. Add a regression test, link code commit to the evidence card and update the confidence label when contradicted.

**Confidence labels:** CONFIRMED (validated contract/original reproducible test); REPORTED (credible source, not independently proven); INFERRED (deduction/other-generation behavior); UNKNOWN (no reliable conclusion); BLOCKED (necessary lawful evidence unavailable).

## Research campaigns, tools, outputs and gates

### R01 — Component/board-revision inventory
**Tools:** public teardowns, official product statements, accessible chip/datasheet information, board photographs with rights, SwitchBrew source history.  
**Method:** produce labeled block diagram: T239, LPDDR5X, storage, display, dock, USB, controllers, audio and radios. Correlate alternative physical part markings without claiming identical revision behavior.  
**Artifact:** board revision matrix and source provenance. **Gate:** no unsourced component assumptions in code.

### R02 — AArch64 architectural differential tests
**Tools:** Clang --target=aarch64-none-elf, LLVM objdump, Arm A-profile reference, first-party interpreter, optionally an independent ARM64 host.  
**Method:** generate tiny original programs for integer arithmetic, condition flags, branches, loads/stores, alignment, atomics/barriers, FP/NEON, traps and supported feature-register behavior. Use exact starting registers, page state, output registers and memory hashes. Compare interpreter/JIT to architectural results.  
**Artifact:** ISA support matrix and machine-code fixture corpus. **Gate:** first original add42 program passes before claiming any CPU emulation. Generic ARM64 results do NOT establish Switch 2-specific extensions.

### R03 — Memory mapping and process ABI
**Tools:** public Horizon notes, original mock process, memory permission/mapping tests, safe fixture generator.  
**Method:** establish guest-visible map/unmap, page sizes (do not assume 4 KiB is all the hardware supports), W^X, shared aliases, allocation address ranges, TLS, exception semantics and virtual vs physical memory distinction. Apply overflow checks and host sandbox boundaries.  
**Artifact:** memory ABI evidence sheets and range/alias/fault regression suite. **Gate:** synthetic guest cannot corrupt host memory through invalid pointers.

### R04 — Horizon kernel/SVC and IPC
**Tools:** SwitchBrew historical SVC documentation, open libnx source as previous-generation context, authored IPC client/server fixture, kernel service mocks.  
**Method:** tabulate SVC numbers, argument registers, error values, handles/rights, buffer directions, session lifetimes, domains and OS version for each service. A Switch 1 value is a hypothesis until a Switch 2-specific source/test supports it.  
**Artifact:** versioned service database with tests for correct and invalid requests. **Gate:** original test clients run deterministically; unknown commands return explicit unsupported results.

### R05 — Executable formats
**Tools:** Clang/LLVM ELF64 toolchain, fuzzing framework, independently authored AArch64 test modules.  
**Method:** start with standard ELF64 PT_LOAD/TLS/relocations and careful bounds validation. Consider public console-specific formats later, only with legitimate documented inputs; don't assume encrypted title containers can be loaded directly.  
**Artifact:** clean original binary corpus, parser and malformed-input tests. **Gate:** a first-party ELF runs through a guest process and exits safely.

### R06 — GPU packet/frontend analysis
**Tools:** public Ampere and NVIDIA driver literature, public Orin/T234 resources (analogy only), synthetic GPU command builder, Vulkan validation.  
**Method:** design an internal synthetic command IR for buffer transfer, clear, triangle, texture, fences and compute. Independently enumerate possible native T239 packet fields and command queue semantics from credible evidence; require original fixtures or trustworthy published structural sources for each native decoder. Do **not** fabricate register values or label internal commands native Switch 2 commands.  
**Artifact:** GPU VA/queue contract, command evidence cards and safe unsupported-opcode handling. **Gate:** guest-derived commands are enabled only when verified.

### R07 — Shader binary and shader translation
**Tools:** LLVM/glslang, shaderc, Khronos SPIRV-Tools, Vulkan validation, original vertex/fragment/compute fixtures.  
**Method:** separate CUDA/PTX vocabulary and SM numbering from unknown proprietary graphics shader encoding. Build typed compiler IR first; emit SPIR-V, validate it and use reference images. Research any actual native shader instruction separately with documented encodings, semantics and independent outcomes.  
**Artifact:** shader IR, SPIR-V backend and golden shader suite. **Gate:** supported native instruction families have verified vectors; unknown binary input rejects safely.

### R08 — Texture format and raster state
**Tools:** original checkerboard/gradient/mip images, CPU reference converters, headless Vulkan, validation and image diffs.  
**Method:** test pitch, tiling/swizzle hypotheses, mip layouts, compression, sRGB, texture sampling, depth/stencil, blending, scissor and barriers. Use measured tolerances rather than visually plausible outputs.  
**Artifact:** per-format CPU/Vulkan golden corpus. **Gate:** exact supported-format coverage is documented and tested across vendors where available.

### R09 — Presentation/timing via normal outputs
**Tools:** standard HDMI capture, settings screens, optional safe external power meter and ordinary temperature monitoring.  
**Method:** observe handheld/docked modes and user-visible frame pacing with controlled temperatures/workloads and repeated sessions; separate capture-device latency from console rendering latency. No internal rail probing or electrical glitch experiments.  
**Artifact:** normal display mode observations, uncertainty and frame-clock tests. **Gate:** GPU/presentation profile reflects observed behavior rather than guessed clocks.

### R10 — Input, controller and audio
**Tools:** SDL3, documented PC gamepad protocols, user-authorized public USB descriptor readers, synthetic sine-wave generator and audio loopback.  
**Method:** support generic controllers and host audio first. Where a physical Joy-Con 2 exposes ordinary safe host HID interfaces, record descriptors and timing without probing protected wireless protocols. Compare hotplug, deadzone, sampling rate and underruns.  
**Artifact:** HID/audio fixture corpus and first-party interactive test demo. **Gate:** stable input/audio HLE even with no physical Switch 2 controller.

### R11 — Filesystem, persistence and error handling
**Tools:** host sandbox filesystem, original I/O test app, crash/restart harness, path normalization and fuzz tests.  
**Method:** implement guest-visible mount paths, read/write/rename, quotas and atomic save transactions. Physical UFS emulation is not necessary for user-level save APIs.  
**Artifact:** deterministic save/reload scenario with host-path isolation. **Gate:** traversal blocked and data survives expected restart.

### R12 — Revision tracking and repeatable science
**Tools:** GitHub issue templates, research cards, fixtures, CTest, sanitized trace summaries and performance dashboards.  
**Method:** record every test's board/OS revision (if actually known), compiler, execution host, protocol version, fixture hash, run count, comparison distribution and uncertainties. Revisit when public documentation updates or new hardware revisions appear.  
**Artifact:** transparent confidence/compatibility matrix. **Gate:** every claimed hardware-specific feature has a source and linked test.

## Evidence card template

~~~text
ID: GPU-QUEUE-001
Subsystem: GPU
Question: Does command A require synchronization B?
Confidence: UNKNOWN / REPORTED / INFERRED / CONFIRMED / BLOCKED
Public sources: exact URL + version + date
Oracle: public architectural spec / permitted original test / host simulation
Hypotheses: H1 and H2, each with distinct predicted output
Original fixture: file path, source hash and build command
Environment: OS, CPU, GPU, device revision if legitimately known
Control groups and repeats: sample size + unchanged variables
Result: observed output, error margin, anomaly/negative cases
Implementation consequence: API, feature flag, tests and commit
Retest trigger: new specification or independently verifiable data
~~~

## Instrumentation boundary

Useful, ordinary instruments: a Windows/Linux developer machine, original AArch64 test compiler, HDMI capture card, public gamepad tester, optional external USB analyzer for safe exposed interfaces, audio capture and reproducible benchmark harness. These are **optional** for foundational CPU, HLE and Vulkan development.

**Not authorized project methods:** defeating secure boot, extracting protected storage, key derivation, exploit chaining, voltage fault injection, secret-protocol compromise, copyrighted code reconstruction or attacking Nintendo services. These are not requirements for this HLE research architecture.

## Reference links

- https://switchbrew.org/wiki/Switch_2:_Hardware
- https://switchbrew.org/wiki/Switch_2:_Tegra_T239
- https://switchbrew.org/wiki/Main_Page
- https://github.com/switchbrew/libnx
- https://developer.arm.com/architectures/cpu-architecture/a-profile
- https://docs.vulkan.org/guide/latest/
- https://github.com/KhronosGroup/SPIRV-Tools
- https://github.com/KhronosGroup/Vulkan-ValidationLayers

**Important:** an emulator can execute original ARM64 test programs and render a synthetic Vulkan triangle long before Switch 2 applications are understood. No native compatibility should be announced without its own verification.
