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
