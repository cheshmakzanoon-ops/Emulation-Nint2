# Synthetic AArch64 guest-service ABI (research fixture only)

This interface is **not** Nintendo Switch 2, Horizon OS, Linux, or any undocumented console ABI. It exists solely to test end-to-end execution of programs we write and own. There is no host system-call passthrough, virtual network, disk access, privileged device access or implicit success for unknown calls.

## Instruction and calling convention

A guest triggers a request with **SVC #0**. Service ID is placed in AArch64 register `x8`; arguments and return values use `x0`, `x1`, `x2`. Only the exact immediate `#0` is recognized; other SVC immediate values and unknown service IDs stop execution with `UnsupportedSupervisorCall` and leave the PC pointing at the SVC instruction.

| x8 service | Inputs | Behavior |
| --- | --- | --- |
| 1: Write | x0 = 1 stdout or 2 stderr; x1 = guest virtual buffer address; x2 = byte length | Copies readable guest memory into the session's captured output. Returns accepted byte count in x0 or unsigned two's-complement negative error code |
| 2: Exit | x0 = status | Stops the session with an explicit exit code. A second `Run` does not re-execute the exit instruction |
| 3: Ticks | none | Returns the number of interpreted guest instructions, including the current SVC, in x0 |

Write error values are `-9` (bad descriptor), `-14` (invalid or unreadable pointer), `-22` (length over 4096 bytes), and `-28` (cumulative output over 65536 bytes). Return values are treated as 64-bit unsigned registers. A failed write does not emit partial output. Both streams share the same output cap. A zero-length write with a valid descriptor is allowed without checking the pointer. Guest code and buffers use the `GuestMemory` read/execute permission system; the session never dereferences guest pointers as native host pointers.

Successful non-exit requests advance PC by four bytes. The exit trap remains at the SVC instruction. CPU faults, unsupported instructions and instruction-budget exhaustion are surfaced separately. The CLI runs at most one million guest instructions; the program and interpreter do not implement a fully compatible AArch64 user-mode process.

## Original integration program

[tests/fixtures/original_hello.s](../tests/fixtures/original_hello.s) is original AArch64 assembly. It addresses its message with an ADR instruction, writes twenty bytes via SVC #0 and exits with status 0. No Nintendo code or assets are used.

Build with CMake/CTest; `nint2_session_tests` generates its own original ELF image in the build directory, then the `cli_guest_elf` CTest verifies `--run-synthetic` output. This works without a cross-compiler and runs on Windows/MSVC and Linux/GCC/Clang. Optional Clang+LLD cross-assembly provides an independent fixture:

```sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld -Wl,-Ttext=0x10000 -Wl,--build-id=none tests/fixtures/original_hello.s -o original_hello.elf
./build/nint2 --run-synthetic original_hello.elf
```

Expected stdout is `Hello from AArch64!` followed by a newline. The CLI reports exit=0, steps=8, pc=0x1001c on stderr. This verifies an independently authored ELF/CPU/SVC integration path; **it is not Switch 2 software or game compatibility**.


## Extension: capability-scoped in-process message queues (2026-10-21)

Services 4–8 operate entirely within a synthetic session; they are **not Horizon kernel calls**, real Switch 2 IPC packets, or software-compatible service IDs. A handle is a 32-bit positive integer scoped to the session. Handles are monotonically allocated and never reused; a closed or unknown handle returns an explicit error. A queue is shared between duplicate handles; rights can only be reduced through duplication.

| x8 | Service | Inputs | Return in x0 |
| --- | --- | --- | --- |
| 4 | CreateQueue | x0=capacity (1–64 messages) | new handle; initially Send, Receive and Duplicate rights |
| 5 | QueueSend | x0=handle, x1=guest source address, x2=bytes (0–4096) | bytes queued; error on a full queue, invalid pointer or missing Send right |
| 6 | QueueReceive | x0=handle, x1=guest destination address, x2=buffer capacity (0–4096) | bytes read; empty and too-small buffer errors do not dequeue |
| 7 | QueueClose | x0=handle | zero if closed |
| 8 | QueueDuplicate | x0=handle, x1=rights mask | new handle if the original has Duplicate and the requested rights are a subset |

Rights bits: Send=1, Receive=2, Duplicate=4. Only mask values 1 through 7 are meaningful; zero or unknown bits cannot be granted. Error values are returned as unsigned 64-bit two's-complement negative integers: -9 EBADF, -13 EACCES, -14 EFAULT, -22 EINVAL, -24 EMFILE, -11 EAGAIN, -90 EMSGSIZE, -12 ENOMEM. The queue supports at most 4096 bytes per message, 65536 queued bytes, and 64 pending messages; maximum handles per session is 256. These are **chosen test limits**, not console specifications.

The receive path peeks without removing the message and calls `GuestMemory::WriteBuffer`, which validates every destination page and prepares backing memory before any bytes are written. A failed guest write leaves the received message queued and prevents a partially modified destination buffer. A send copies all bytes out of verified readable guest memory before enqueueing. There is no host networking, OS-native IPC, blocking wait, cross-process handle passing, or hardware-observed guest ABI yet. This phase is deterministic, single-threaded and does not model real Horizon scheduling.

### Reproducible independent guest IPC demonstration

`tests/fixtures/original_queue.s` is original ARM64 assembly which creates a queue, enqueues 18 bytes from an RX text segment, receives the bytes into a separate writable BSS segment, writes the received message to synthetic stdout, closes the handle and exits with zero. The expected stdout is `Hello via kernel!` followed by a newline, and the program executes **25 guest instructions**.

To compile its own ELF independently of the test's hand-encoded machine words:

```sh
clang --target=aarch64-none-elf -nostdlib -fuse-ld=lld \
  -Wl,-Ttext=0x10000 -Wl,--build-id=none -Wl,-z,max-page-size=4096 \
  tests/fixtures/original_queue.s -o original_queue.elf
./build/nint2 --run-synthetic original_queue.elf
```

CTest also generates `synthetic_ipc.elf` in the **build directory** using `nint2_ipc_guest_tests --emit-elf` and runs it through the normal `nint2 --run-synthetic` CLI, so the cross-compiler is not required in CI. The fixture generation is an explicit CTest dependency. Source and generated binaries are independent and contain no Nintendo code or assets.


## Compiler-generated signed data and IPC fixture (2026-10-21)

**tests/fixtures/original_compiled_ipc.c** is original freestanding C for this
explicitly synthetic test ABI. It uses Clang-generated signed LDRSB/LDRSH
instructions, array iteration, integer multiply, stack frames, subroutine calls
and SVC, validating the signed dot product equals -116 before creating a queue.

The program creates a two-message queue (service 4), sends a 22-byte constant
(service 5), receives it into an independent writable BSS buffer (service 6),
compares each byte, writes the received bytes (service 1), closes the handle
(service 7) and exits zero (service 2). A tampered signed-byte constant is
expected to exit 91 without output or handles.

**tests/compiled_ipc_guest_tests.cpp** emits a new static guest ELF from its
own AArch64 machine words and data into the build directory, then tests the
in-process runtime and the standard --run-synthetic CLI via CTest. The new
checks are awaiting completed remote CI validation.

These services and instruction tests are **not Horizon IPC**, native Nintendo
software, measured Switch 2 behavior, or full AArch64 ISA emulation.

## Host-only multi-session scheduling and queue authority (research extension)

`SyntheticSession::GrantQueueTo(source_handle, recipient, rights)` and
`kernel::HandleTable::GrantTo` allow a **host test controller** to copy a queue
reference into another isolated guest's handle table. The original handle must
have Duplicate right and may grant only a subset of its rights. An invalid,
stale or nonduplicable source fails; a full recipient table fails without
revoking the source. Recipients have independent, monotonically allocated handle
IDs. Closing the source handle does not revoke independently granted handles.
There is no guest-visible handle-transfer service and **no Horizon ABI claim**.

`SyntheticScheduler` accepts at most 16 preconstructed `SyntheticSession`
references and executes each in deterministic round-robin instruction quanta
(default 1 instruction, configurable). A `Run` call has an aggregate instruction
budget across all sessions; repeated calls preserve the next scheduled process
and cumulative count. Guest faults only terminate that guest; the result reports
number of successful exits and failures. Separate sessions own separate
`GuestMemory` instances, CPU registers, virtual ticks and captured output;
only explicitly granted queues share message storage. The caller owns session
lifetimes and is responsible for granting capabilities.

`tests/fixtures/original_sender.s` and
`tests/fixtures/original_receiver.s` provide original source examples:
CreateQueue, host Receive-only grant, send `PING!`, first empty Receive returning
`-11`, retry, write received buffer, close producer handle and exit.
`nint2_multi_guest_elf_tests` can construct and load original fixture ELFs
without a cross-compiler, emit those ELFs into the build directory, and accept
independently cross-linked sender/receiver ELF paths with `--run-pair`.
All results remain subject to CI validation. This feature is not true guest
thread preemption, native services, real console scheduling or a kernel port.

## New AArch64 original test-only integer instructions

The ARM64 reference interpreter now includes ADC/ADCS/SBC/SBCS carry arithmetic,
UDIV/SDIV, variable shifts and rotate, and EXTR/ROR immediate. These are
ordinary ARM64 instruction semantics, not new synthetic SVC services or
claims about Nintendo hardware. No changes were made to the eight guest test
service numbers or handle/IPC rights contracts. Optimized original C programs
exercise the new instructions through the existing ELF loader and explicit
synthetic Write/Exit calls (Linux CI verification pending).
