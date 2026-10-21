# Provenance, licensing and clean-room research policy

## Scope

Emulation-Nint2 is a study of independently recreating application-visible behavior, not a vehicle for redistributing Nintendo intellectual property or bypassing console protections. The master development plan and hardware research protocol are controlling references.

## Permitted inputs

- Public architecture and standards documents, with URLs/revisions/attribution.
- Licensed open-source tools and interfaces after version/license audit.
- Original compiler-generated AArch64 programs, generated images, tones and fixtures.
- Ordinary hardware output observations made with lawfully owned equipment and the necessary authorization.
- Publicly documented software API behavior, tracked with confidence grades.

## Excluded from the repository and test infrastructure

- Proprietary firmware/system dumps, game binaries/assets, confidential SDKs, encryption keys or copied decompiled code.
- Console security bypass/exploit instructions, secrets from privileged hardware interfaces, unauthorized Nintendo service interactions.
- Unredacted personal identifiers, credentials, protected network traffic or opaque traces whose license/provenance cannot be established.

## Research separation

For each Switch 2-specific feature, create an evidence record under docs/research/ with claim, source, independent test design, confidence, version scope, uncertainty and implementation consequences. A public Switch 1 or NVIDIA T234 implementation is a lead, not confirmed Switch 2 behavior. Where code has ambiguous provenance, isolate the research claim in human-readable behavioral terms and require independent implementation review.

## Third-party licensing

The repository owner has **not selected a final project license**. Public visibility alone does not grant a reuse license. Before accepting outside contributions or importing code, the maintainer must decide the repository's intended outbound license and audit compatibility of every dependency/fork/submodule. Keep dependency versions pinned, SPDX/license notices, copyright attributions, and a software bill of materials. Dynarmic, Vulkan components and prior emulator projects must each be reviewed at the exact imported revision; similarly named forks can have different licenses.

## Publication checklist

- [ ] No prohibited payloads or private IDs in source, assets, docs, artifacts or traces.
- [ ] Every external dependency and borrowed snippet has its proper license notice.
- [ ] Every native device claim is documented with a confidence label and traceable source.
- [ ] Every shipped emulator feature has tests and limitations documented in STATUS.md.
- [ ] No unsupported claim of Nintendo Switch 2 retail-game compatibility.
- [ ] Final release license and jurisdiction-specific legal review completed.


## Original CPU operand regression sources — 2026-10-21

The SP/WSP/ZR, logical ROR and LSLV corrections reference Arm's public
[Instruction Set Reference Guide, 100076_0100_00_en](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab),
sections D2.10 (ADDS immediate), D2.14–15 (AND immediate/shifted), D2.78–79
(EOR immediate/shifted), and D2.95 (LSLV). The implementation and assembly
fixtures were written independently; no external emulator code was imported.
`tests/verify_assembler_fixtures.py` checks the original assembly encodings
against Clang's assembler; C++20 `std::rotr` and literal arithmetic expectations
provide separate value oracles. These are AArch64 architectural claims, not
measurements of Nintendo Switch 2 hardware.


## Original compiled-C request/reply corpus — 2026-10-21

`original_rpc_client.c`, `original_rpc_server.c` and `original_rpc.h` are original
freestanding C programs written for the documented synthetic ABI. The packet
protocol, report layout, checksums and host digest oracle are independently
authored project fixtures, not recovered console interfaces. The checked-in
`compiled_rpc_images.hpp` contains only load bytes compiled from those sources
using Ubuntu Clang/LLD 18.1.3 at `-O1 -mgeneral-regs-only`; it imports no guest
runtime library. `tests/regenerate_rpc_fixtures.py --clang clang-18 --lld ld.lld-18 --check`
reproduces the bytes. `tests/verify_compiled_guests.py` executes independently
linked files through the actual loader/interpreter and validates the result.

The CLI gate correction follows CMake's documented
[PASS_REGULAR_EXPRESSION behavior](https://cmake.org/cmake/help/latest/prop_test/PASS_REGULAR_EXPRESSION.html):
that property ignores the process exit code. Original temporary regression
programs verified the false-success case; committed CMake wrappers now require
both exact stdout and a successful process exit.
