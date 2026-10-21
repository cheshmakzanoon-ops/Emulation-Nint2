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
