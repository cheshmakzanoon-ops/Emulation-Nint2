# Emulation-Nint2 — architecture and decisions

## Decision 1: a user-mode HLE emulator, not a secure-boot simulator

The long-term direction is an independent AArch64 application execution runtime and an observable approximation of validated Horizon services. The implemented runtime currently executes original ELF programs through a narrow reference interpreter and explicitly synthetic services; it does not implement Horizon or establish native Switch 2 compatibility. Reproducing transistor-level hardware, memory encryption or proprietary boot firmware is not necessary for this roadmap.

## Decision 2: explicit module boundaries

The implemented original-program path has the following boundaries:

| Component | Implemented responsibility |
| --- | --- |
| CLI and host controller | Read bounded original ELF files, provision isolated sessions and explicit queue capabilities, collect results |
| `LoadElf64` | Validate static AArch64 ELF PT_LOAD segments, map permissions and initialize guest memory |
| `GuestMemory` | Separate virtual address spaces, checked access, lazy data backing, coherent aliases and cross-page operations |
| `CpuInterpreter` | Execute a tested subset of AArch64 integer instructions and surface unsupported instructions or memory faults |
| `SyntheticSession` | Own CPU state, captured output and local handles; dispatch the original SVC protocol |
| `kernel::HandleTable` and `MessageQueue` | Enforce capability rights, bounded FIFO messages and shared queue lifetimes |
| `SyntheticScheduler` | Run independently owned sessions in deterministic instruction quanta with an aggregate budget |

Guest data passes through bounded memory APIs; queue sharing never grants
access to another session's memory. A session's Exit status halts execution;
the host owns session lifetimes, and handles remain owned until explicit close
or destruction. Full process/thread lifecycle management is still planned.

Future HLE services, a JIT, GPU packet decoding, shader IR, Vulkan rendering,
filesystem, audio and input belong behind separate interfaces. CPU execution
and service dispatch must not call Vulkan directly. Any future GPU decoder
requires evidence of its input format before producing renderer IR.

## ADR-001: C++20 / CMake / CTest

Natively portable Windows x64 and Linux x64 target. Bootstrap library and smoke CLI have **no external dependencies**. Later introduce optional pinned Dynarmic, Vulkan, SPIRV-Tools, SDL3, and testing utilities after version/license review.

## ADR-002: a reference interpreter before an optimized JIT

Implement a small, precise AArch64 interpreter for arithmetic/branches/memory/control-flow as a test oracle. Attach the JIT through CpuBackend after published tests pass. Maintain fallback or clear exceptions for missing ISA features, including possible ARMv8.3+ instructions.

## ADR-003: Guest memory is separate from host memory

`GuestMemory` implements sparse page metadata, 4 KiB mappings, lazy data backing,
checked scalar and buffer access, executable fetch, coherent aliases and W^X
checks. Distinct session memory objects do not share pages implicitly. The
synthetic pair CLI reserves a 1 GiB virtual address range per guest without
allocating that amount of data. JIT invalidation is future work because there
is no integrated JIT.

## ADR-004: service contracts are versioned

Kernel objects -> SVC dispatch -> IPC transport -> HLE service manager. Track profiles and public/experimental interfaces with source references, errors and tests. Switch 1 Horizon implementation is historical context, not authoritative Switch 2 behavior.

## ADR-005: renderer intermediate representation

Guest GPU packet dialect is not Vulkan, CUDA or SPIR-V. Distinct steps: documented packet decode, GPU virtual-memory operations, render IR, guest shader parse/IR, SPIR-V emission and validation, Vulkan resource/synchronization management, present. Build host Vulkan synthetic demos before native guest decoding. Unknown methods are blocked, never mapped to guessed functionality.

## ADR-006: deterministic diagnostics

The original fixture corpus records deterministic instruction counts and guest
outcomes. Compiled C request/reply tests replay identical scheduling inputs and
compare reports and digests. Full debugger, thread-aware tracing and GPU event
logging remain planned. Corpus inputs are original programs; unsupported
instructions and unknown services surface explicit stops.

## ADR-007: host bootstrap for original C request/reply guests

`SyntheticSession::ImportQueue(owner, handle, rights)` imports an attenuated
queue capability from a host-owned handle table. It uses the same Duplicate
authority and rights-subset checks as `GrantTo`, preserves independent handle
IDs, and executes no guest instructions. This allows independently compiled
programs to start with provisioned queues without stepping a compiler-specific
number of startup instructions.

`nint2 --run-synthetic-pair <client.elf> <server.elf>` creates two isolated guest
address spaces and two capacity-one queues. The client receives Send-only
request and Receive-only reply authority; the server receives the opposite
directions. The host closes its bootstrap handles after importing them. Each
guest starts with its send handle in `x0`, receive handle in `x1`, and a private
64-byte report pointer in `x2`. The CLI uses `0x3ffe0000` for the report mapping;
the fixture convention also accepts zero to disable reporting. Private stacks
occupy `0x3fff0000`–`0x3fffffff`, with initial SP `0x40000000`.

The CLI distributes at most one million guest instructions in round-robin
quanta of 64. A successful command requires two zero-status guest exits and
no scheduler-reported guest fault. The reports record completed transactions,
digest, retry counts, capability-denial checks and exit status; their last two
fields are explicit test controls for a corrupted request and a nonzero final
exit. See [SYNTHETIC_ABI.md](SYNTHETIC_ABI.md#optional-private-report) for the
complete field offsets and startup contract.

The [original client](../tests/fixtures/original_rpc_client.c) and
[server](../tests/fixtures/original_rpc_server.c) perform 64 bidirectional
transactions. [Their test harness](../tests/compiled_rpc_guest_tests.cpp)
checks independent host arithmetic, replay, separate memory, reduced rights,
queue backpressure and negative paths. Original compiler-generated load bytes
can be regenerated and verified using
[regenerate_rpc_fixtures.py](../tests/regenerate_rpc_fixtures.py); no console
software, guest runtime library or platform firmware is included.

Queue calls are nonblocking, and retries consume guest instructions. There are
no blocking wait queues or peer-close notifications. In the corrupted-request
test the server exits and the peer's retry loop reaches the aggregate budget;
this is a bounded failure, not deadlock detection. The synthetic protocol is
an integration fixture rather than a claim of native kernel compatibility.

## Dependency sequencing

- Memory and interpreter -> CPU ISA tests -> guest ELF loader -> kernel SVC/IPC -> original text demo.
- Vulkan bootstrap -> virtual GPU queues -> documented GPU command IR -> SPIR-V pipeline -> original triangle demo.
- HLE + verified GPU + audio/input -> original interactive guest program.
- Native Switch 2 compatibility is **blocked** wherever the needed lawful evidence or test oracle does not exist.

## Planned interfaces (not yet implemented)

Full CpuBackend/JIT integration; GuestProcess and guest threads; blocking
synchronization; validated Horizon SVC and IPC transport; ServiceManager;
GpuVaSpace; GuestGpuFrontend; ShaderIR; VulkanRenderer; AudioSink; InputSource;
SandboxVfs; full EmulatorSession lifecycle. Existing `GuestMemory`,
`SyntheticSession`, `kernel::HandleTable` and `SyntheticScheduler` are limited
implemented foundations, not completed versions of all these interfaces.

## Immediate technical debt

Broader ISA coverage, independent CPU conformance, loader fuzzing, JIT
integration, guest threading and synchronization, lifecycle notification and
validated native service contracts remain unfinished. The loader supports
bounded static ELF fixtures, not dynamic linking or Nintendo executable
containers. There is no implemented renderer, audio or input stack. Current
CLI modes execute original ELF/CPU/SVC tests, including compiled C IPC;
these results do not establish native Switch 2 software compatibility.

## ADR-008: observable cooperative liveness and hermetic asset IO

The synthetic reference runtime now has queue peer counts, a test-only
QueueWait service, explicit `SessionStop::Blocked`, resumable scheduler
deadlock reporting, and endpoint retirement on process exit. This is separate
from future Horizon thread scheduling and native synchronization objects.
Existing QueueSend/QueueReceive retain their nonblocking behavior.

A separate `assets::AssetStore` supports host-provisioned **immutable**
copies of original fixture resources in a sealed, per-process namespace.
File handles use a disjoint numeric range from queue handles; path parsing
is ASCII-only and rejects traversal, and all guest output buffers are validated
through `GuestMemory`. No guest filename can access the host filesystem.
The explicit CLI import is a host operation, not a guest path mapping.
A full SandboxVfs compatible with any console service interface is still
planned. New features are implemented but remote CI remains pending.
