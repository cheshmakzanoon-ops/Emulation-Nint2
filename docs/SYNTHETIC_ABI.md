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

The receive path peeks without removing the message and calls `GuestMemory::WriteBuffer`, which validates every destination page and prepares backing memory before any bytes are written. A failed guest write leaves the received message queued and prevents a partially modified destination buffer. A send copies all bytes out of verified readable guest memory before enqueueing. There is no host networking, OS-native IPC, blocking wait, guest-visible cross-session handle transfer, or hardware-observed guest ABI yet. Host-authorized queue sharing is described below. This runtime is deterministic, single-threaded and does not model real Horizon scheduling.

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

`SyntheticSession::ImportQueue(owner, source_handle, rights)` imports a queue
from a host-owned `kernel::HandleTable` into the receiving session. It delegates
to the same checked `GrantTo` operation: the source requires Duplicate authority,
requested rights must be a valid subset, and recipient exhaustion cannot revoke
the source or alter existing imports. Importing a queue does not execute guest
instructions or change CPU registers. The controller receives the process-local
handle ID and can place it in the chosen startup registers before execution.
This avoids depending on a fixed count of compiler-generated startup instructions.
The owner may close its handles after provisioning; imported references keep the
queue alive. [Session import tests](../tests/session_import_tests.cpp) cover rights,
independent IDs, lifetime, full handle tables and recovery.

`SyntheticScheduler` accepts at most 16 preconstructed `SyntheticSession`
references and executes each in deterministic round-robin instruction quanta
(default 1 instruction, configurable). A `Run` call has an aggregate instruction
budget across all sessions; repeated calls preserve the next scheduled process
and cumulative count. Guest faults only terminate that guest; the result reports
the number of exits and faults. An exit can carry a nonzero status; callers must
inspect each guest's exit code before treating the run as successful. Separate sessions own separate
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

## Original compiled C request/reply protocol

The [client](../tests/fixtures/original_rpc_client.c) and
[server](../tests/fixtures/original_rpc_server.c) are independently compiled
freestanding AArch64 C programs using only the existing synthetic services 1–8.
Their [shared source header](../tests/fixtures/original_rpc.h) defines a test
startup convention and a 32-byte packet with four little-endian `uint64_t`
fields: sequence number, value, bitwise inverse of the value, and checksum.
The client sends 64 requests, validates every transformed reply, and accumulates
a digest; the server independently validates requests and computes replies.
The client sends two requests before receiving two replies, exercising
backpressure in queues with capacity one. These packet fields and startup
registers are original test contracts, not native console interfaces.

### CLI startup and host provisioning

```sh
python3 tests/regenerate_rpc_fixtures.py --compile-only --output-dir build/rpc
./build/nint2 --run-synthetic-pair \
  build/rpc/original_rpc_client.elf build/rpc/original_rpc_server.elf
```

[The generator](../tests/regenerate_rpc_fixtures.py) uses Clang/LLD with `-O1`
and `-mgeneral-regs-only`; `--check` compares the recorded Clang 18 PT_LOAD bytes
with fresh compiler output. [The C++ harness](../tests/compiled_rpc_guest_tests.cpp)
can also emit original ELFs from the checked-in
[load images](../tests/fixtures/compiled_rpc_images.hpp) with `--emit-elf`, allowing
the runtime tests to run without a cross-compiler. Its `--run-pair` mode accepts
the independently linked ELF files and runs the same assertions.

The [pair CLI](../src/app/synthetic_pair.cpp) loads two ELF files of at most
32 MiB each into separate `GuestMemory` objects, each with a 1 GiB virtual
address-space limit. Each guest receives its own 64 KiB read/write stack at
`0x3fff0000`, with initial SP `0x40000000`, and a private read/write report page
at `0x3ffe0000`. An ELF that overlaps either reserved mapping is rejected.
Equal virtual addresses do not share backing memory.

The host creates a request queue and a reply queue, each with capacity one. It
imports Send-only request authority and Receive-only reply authority into the
client, and the opposite directions into the server. No imported capability
has Duplicate authority. The bootstrap owner then closes both of its handles;
the guests retain the queue references needed for the exchange.

| Startup register | Meaning in each guest |
| --- | --- |
| `x0` | Its process-local Send-only queue handle |
| `x1` | Its process-local Receive-only queue handle |
| `x2` | Guest pointer to an optional private 64-byte report; the CLI supplies `0x3ffe0000`. A host test may supply zero to disable reporting and fault controls |

The CLI uses an aggregate **1,000,000-instruction budget** for both guests and
a **64-instruction round-robin quantum**. It captures each guest's output and
prints the client output followed by the server output; this is not a merged
instruction-by-instruction output trace. Success requires scheduler completion,
two exits, no guest fault, and both exit statuses zero. The CLI does not interpret
the report or assert that arbitrary input ELFs implement this RPC fixture;
packet counts, digest and expected output are checked by the test harness.

### Optional private report

The report contains eight little-endian `uint64_t` fields. The guest clears
offsets 0–40 at startup. The host initializes the final two control fields
before execution; the CLI leaves them zero. Each guest has its own report,
even when both pointers have the same virtual address.

| Byte offset | Field | Meaning |
| ---: | --- | --- |
| 0 | `completed` | Last successfully processed request or verified reply, up to 64 |
| 8 | `digest` | Accumulated digest of processed reply values |
| 16 | `send_retries` | Send calls returning `-11` because the queue is full |
| 24 | `receive_retries` | Receive calls returning `-11` because the queue is empty |
| 32 | `rights_denials` | Successful checks that wrong-direction capabilities return `-13`; expected value 2 |
| 40 | `exit_code` | Status recorded immediately before synthetic Exit |
| 48 | `corrupt_sequence` | Client-only test control: a sequence from 1–64 whose outgoing request checksum is corrupted; zero disables it |
| 56 | `final_exit_code` | Test control: exit with this status after the normal success output, to verify that output alone cannot establish success |

The harness tests quanta 1, 7, 64 and 257 with repeated budgets of at most 997
instructions per scheduler call and a 200,000-instruction aggregate limit. It
compares both guest digests with a separate host calculation, replays identical
schedules, checks private sentinels, requires both guests to close their handles,
and tests reporting disabled. It also verifies the exact corrupted-request error
path and rejects success-looking output followed by a nonzero exit.

### Retry and termination limits

Send and Receive remain **nonblocking**. Full or empty queues return `-11`, and
the original C fixtures retry in guest code, consuming ordinary instruction
budget. There are no blocking waits, timeouts, peer-close notifications, or
automatic error replies. Closing one handle leaves other queue references
valid. Exiting a session stops execution but does not itself clear its handle
table; normal RPC completion explicitly closes both guest handles.

When the test corrupts request 7, the server exits with status 114 after six
successful requests. Its peer continues retrying and is stopped by the test's
aggregate instruction budget. That result is an expected bounded failure,
not successful completion or implemented deadlock detection. Actual blocking
synchronization and peer lifecycle notifications remain future work.

## New AArch64 original test-only integer instructions

The ARM64 reference interpreter now includes ADC/ADCS/SBC/SBCS carry arithmetic,
UDIV/SDIV, variable shifts and rotate, and EXTR/ROR immediate. These are
ordinary ARM64 instruction semantics, not new synthetic SVC services or
claims about Nintendo hardware. No changes were made to the eight guest test
service numbers or handle/IPC rights contracts. Optimized original C programs
exercise the new instructions through the existing ELF loader and explicit
synthetic Write/Exit calls. Local execution has been verified; remote CI
results are tracked in STATUS.md.

## Extension: cooperative QueueWait and explicit peer lifecycle (pending CI)

Service **9 QueueWait** is *test-only*, not a Horizon OS syscall. It takes
`x0=queue handle`, `x1=direction` (1=Send, 2=Receive) and `x2=mode`
(0=poll, 1=cooperatively wait). A ready queue returns zero. Empty Receive or
full Send returns -11 (EAGAIN) in polling mode. In waiting mode, an unready
queue returns `SessionStop::Blocked`, leaves PC on SVC and requires no host
thread. Re-running the same guest before queue state changes consumes **zero
additional instructions**. A scheduler skips blocked guests and returns
`SchedulerStop::Deadlock` if no runnable guest remains; this is resumable
after an explicit host or peer state change.

When the last opposite-direction endpoint retires, QueueWait returns -32
(EPIPE), except that previously buffered data stays readable until drained.
Handle close, host-controlled process retirement and handle-table destruction
decrement the corresponding peer counts. Terminal sessions retain their
handle count for diagnostics, but their endpoints are no longer active. Queue
Send and Receive (services 5/6) remain nonblocking and keep their original
EAGAIN behavior, so existing independently compiled C guests are not silently
changed. Unrecognized modes/rights return -22; insufficient rights return -13.
This is a deliberately small cooperative research runtime with no preemption,
futexes, OS threads, priority inheritance, or native console scheduler.

Independent assembly sources `wait_sender.s` and `wait_receiver.s` exercise
a true blocked ARM64 guest, interleaved peer Send/Receive, host wake-up,
deadlock detection, full queues and peer departure.

## Extension: sealed synthetic read-only asset namespace (pending CI)

This original namespace is **not** the Switch 2 filesystem or a host-file
passthrough. A host calls `SyntheticSession::MountAsset(path, bytes)` before
running guest code. Each process has independent copied asset bytes and
descriptors. The manifest seals at the first `Run()`. Filenames are rooted
ASCII paths up to 128 bytes, using letters, digits, period, underscore and
hyphen. Empty segments, `.`, `..`, double slash, trailing slash, host
backslash and control bytes are rejected; entries are sorted lexically.

Resource limits: 64 files, maximum 1 MiB per file, 16 MiB total, 128 open
handles (starting at 0x10000, never reused), 4096 bytes per read, 1 TiB maximum
virtual seek. All successful guest copies go through `GuestMemory::WriteBuffer`,
which checks full destination permissions before changing guest memory.
A failed Read leaves its per-descriptor file offset unchanged. Bytes are
unmodified and each file has a deterministic FNV-1a 64 hash (not cryptographic).

| x8 | Service | Input registers | x0 return |
| --- | --- | --- | --- |
| 10 | OpenAsset | x0=readable guest path ptr, x1=path length, x2=0 (read only) | file handle or negative error |
| 11 | ReadAsset | x0=fd, x1=writable guest ptr, x2=bytes | bytes transferred (0 means EOF) or error |
| 12 | SeekAsset | x0=fd, x1=signed two's-complement offset, x2=origin (0 begin, 1 current, 2 end) | new offset or error |
| 13 | CloseAsset | x0=fd | 0 or error |
| 14 | StatAsset | x0=path ptr, x1=length, x2=destination for 16 bytes | 0 or error |
| 15 | EnumerateAsset | x0=sorted zero-based index, x1=record ptr, x2=buffer capacity | 1 for entry, 0 at end, or error |
| 16 | ReadAtAsset | x0=fd, x1=buffer, x2=count, x3=offset | bytes read without changing cursor |
| 17 | FstatAsset | x0=fd, x1=24-byte destination | 0 or error |

`StatAsset` stores two little-endian u64 fields: file size, file digest.
`FstatAsset` appends the independent descriptor cursor as a third u64.
`EnumerateAsset` writes an all-or-nothing 152-byte record containing size
(u64), hash (u64), path byte count (u64), and 128 bytes of zero-padded path
data. EOF of the enumeration returns 0 without touching the guest pointer.
Only x0=0 is a valid Open mode; zero-byte reads also succeed at EOF.

Relevant negative results include -2 ENOENT, -9 EBADF, -14 EFAULT,
-22 EINVAL, -24 EMFILE, and -90 EMSGSIZE. No service reads, creates, modifies
or enumerates host files by guest-requested paths. In CLI mode only the host
explicitly provides a single input file:

```sh
./build/nint2 --run-synthetic-asset build/original_asset_guest.elf \
  /demo/message tests/fixtures/asset_message.txt
```

This specific source-controlled guest expects a private writable guest
buffer in x20, provisioned by the CLI at 0x3fff0000, and prints
`Loaded from asset!` before exiting zero. Other original applications must
define their own bootstrap convention. The architecture provides no general
native filesystem ABI, permissions database, symlinks, host path mapping,
storage persistence, compressed archives or Nintendo content loader.
