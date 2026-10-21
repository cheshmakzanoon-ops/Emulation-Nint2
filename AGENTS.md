# Instructions for development agents and contributors

1. **Single main branch.** Inspect the latest state; make small coherent commits on main and push them. Never force-push, delete user work, rewrite history or claim changes that were not committed.
2. **Reality over placeholders.** Follow docs/MASTER_DEVELOPMENT_PLAN.md in dependency order. Every phase has tests and exit criteria. Do not claim Switch 2 game/firmware compatibility from a file parser, a CLI or a fabricated GPU opcode.
3. **Truthful status.** Update docs/STATUS.md for every milestone with changed files, commands actually executed, test results, commit and blocked work. Distinguish planned / partial / tested / validated on native hardware.
4. **Research evidence.** Before implementing an undocumented device behavior, create a research card per docs/HARDWARE_RESEARCH.md with sources, date, confidence, a falsifiable hypothesis and lawful original fixtures. T234, Ampere and Switch 1 behavior are hints, not proof of Switch 2 behavior.
5. **Security and rights.** Never commit games, firmware, console keys, protected dumps, copyrighted assets, confidential SDKs or instructions to circumvent protections. Use only public specifications and independently written test programs. Sandbox guest paths; bounds-check every guest pointer and input length.
6. **Engineering methodology.** Original fixture -> failing test -> implementation -> validation -> documentation -> commit. Prefer testable end-to-end behavior over expansive scaffolds. Add regression tests for every bug.
7. **Tools.** C++20, CMake, CTest. Windows x64 and Linux x64 CI are primary. Keep bootstrap buildable without network/proprietary dependencies. Pin introduced libraries and review their exact licenses.
8. **CPU.** First build small AArch64 reference interpreter and original ELF test corpus. Add vetted Dynarmic backend after ISA-feature and license review. Unsupported instructions must produce explicit exceptions.
9. **Kernel.** Handles, scheduling, SVC and IPC are separately testable. Service contracts are explicitly versioned; no guessed silent success.
10. **Graphics.** First implement a host-owned Vulkan triangle. Native guest GPU packet/shader decoding requires evidence; unsupported codes remain unimplemented. SPIR-V tools validate output, not unidentified input ISA.
11. **Quality.** Run CMake configure/build and CTest for each code change. Add ASan/UBSan and fuzzing for parsers. Report what was not tested, including Windows hardware if unavailable.
12. **Licensing.** Final repo license choice is unresolved. Do not import prior emulator code or third-party code whose license may conflict. Record notices and provenance of approved dependencies.
