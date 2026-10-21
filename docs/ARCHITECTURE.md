# Emulation-Nint2 — architecture and decisions

## Decision 1: a user-mode HLE emulator, not a secure-boot simulator

Build an independent AArch64 application execution runtime and an observable approximation of Horizon services. Reproducing transistor-level hardware, memory encryption or proprietary boot firmware is not necessary for this roadmap. Research remains useful even if native Switch 2 programs cannot currently be run lawfully as test fixtures.

## Decision 2: explicit module boundaries

~~~text
Desktop frontend / CLI
        |
Emulator session & lifecycle
        |
  +-----+------+------------+------------+
  |            |            |            |
CPU backend  Guest memory  Kernel/IPC   GPU frontend
  |            |            |            |
Interpreter   Page map    HLE services  Guest command decoder (evidence gated)
Dynarmic JIT   VM faults   VFS / clock   Typed renderer IR / shader IR
                                           |
                                  Host Vulkan renderer
Host platform: filesystem sandbox, input, audio, timers, logging
~~~

The CPU and HLE layers do not call Vulkan directly. Guest data must pass through bounded memory APIs. Kernel owns handles and process/thread lifetime. GPU frontend parses verified guest commands and produces typed IR. Renderer executes validated IR and never guesses guest command formats.

## ADR-001: C++20 / CMake / CTest

Natively portable Windows x64 and Linux x64 target. Bootstrap library and smoke CLI have **no external dependencies**. Later introduce optional pinned Dynarmic, Vulkan, SPIRV-Tools, SDL3, and testing utilities after version/license review.

## ADR-002: a reference interpreter before an optimized JIT

Implement a small, precise AArch64 interpreter for arithmetic/branches/memory/control-flow as a test oracle. Attach the JIT through CpuBackend after published tests pass. Maintain fallback or clear exceptions for missing ISA features, including possible ARMv8.3+ instructions.

## ADR-003: Guest memory is separate from host memory

Virtual memory must enforce guest permissions, page mapping, checked arithmetic, read/write and executable transitions, shared aliases and proper invalidation of translated code. Do not eagerly allocate 12 GB. **Current M0 code is a basic page-eager 4 KiB demonstrator, not production sparse VM.**

## ADR-004: service contracts are versioned

Kernel objects -> SVC dispatch -> IPC transport -> HLE service manager. Track profiles and public/experimental interfaces with source references, errors and tests. Switch 1 Horizon implementation is historical context, not authoritative Switch 2 behavior.

## ADR-005: renderer intermediate representation

Guest GPU packet dialect is not Vulkan, CUDA or SPIR-V. Distinct steps: documented packet decode, GPU virtual-memory operations, render IR, guest shader parse/IR, SPIR-V emission and validation, Vulkan resource/synchronization management, present. Build host Vulkan synthetic demos before native guest decoding. Unknown methods are blocked, never mapped to guessed functionality.

## ADR-006: deterministic diagnostics

Logger and tracing APIs must record guest thread, virtual time, memory map ID, syscall and GPU packet class. Corpus contains only original test inputs; invalid content must fail safely. Runtime can replay deterministic fixture results and produce redacted reports.

## Dependency sequencing

- Memory and interpreter -> CPU ISA tests -> guest ELF loader -> kernel SVC/IPC -> original text demo.
- Vulkan bootstrap -> virtual GPU queues -> documented GPU command IR -> SPIR-V pipeline -> original triangle demo.
- HLE + verified GPU + audio/input -> original interactive guest program.
- Native Switch 2 compatibility is **blocked** wherever the needed lawful evidence or test oracle does not exist.

## Planned interfaces (not yet implemented)

GuestAddressSpace; CpuBackend; GuestProcess; KernelHandleTable; Scheduler; SvcDispatcher; IpcTransport; ServiceManager; GpuVaSpace; GuestGpuFrontend; ShaderIR; VulkanRenderer; AudioSink; InputSource; SandboxVfs; EmulatorSession.

## Immediate technical debt

Current GuestMemory lacks page aliasing, lazy backing, executable fetch, typed multibyte memory ops, exception delivery and large virtual maps. Current CLI runs only native smoke tests. Neither is evidence of native Switch 2 execution.
