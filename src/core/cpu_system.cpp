#include "nint2/cpu.hpp"
#include <cstdint>
#include <optional>

namespace nint2 {
// Narrow architectural EL0 system-register model. Unrecognized/privileged
// registers trap rather than leaking host state or fabricating hardware.
std::optional<CpuResult> CpuInterpreter::StepSystem(std::uint32_t op) {
    const bool read = (op & 0xfff00000U) == 0xd5300000U;
    const bool write = (op & 0xfff00000U) == 0xd5100000U;
    if (!read && !write) return std::nullopt;
    const auto pc = context.pc;
    const unsigned rt = op & 31U;
    const auto done = [&]() -> CpuResult {
        context.pc = pc + 4;
        return {CpuStop::Running, 1, 0, op};
    };
    const auto invalid = [&]() -> CpuResult {
        return {CpuStop::UndefinedInstruction, 1, pc, op};
    };
    switch (op & 0xffffffe0U) {
    case 0xd53b4200U: // MRS Xt, NZCV
        WriteReg(rt, std::uint64_t{context.nzcv & 15U} << 28, true);
        return done();
    case 0xd51b4200U: // MSR NZCV, Xt
        context.nzcv = static_cast<std::uint8_t>((ReadReg(rt, true) >> 28) & 15U);
        return done();
    case 0xd53bd040U: // MRS Xt, TPIDR_EL0
        WriteReg(rt, context.tpidr_el0, true);
        return done();
    case 0xd51bd040U: // MSR TPIDR_EL0, Xt
        context.tpidr_el0 = ReadReg(rt, true);
        return done();
    default:
        return invalid();
    }
}
} // namespace nint2
