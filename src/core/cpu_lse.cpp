#include "nint2/cpu.hpp"
#include <cstdint>
#include <optional>

namespace nint2 {
// ARMv8.1 LSE operations for a deterministic single-host-thread interpreter.
// These are architectural A64 opcodes, not Nintendo/Horizon interfaces.
std::optional<CpuResult> CpuInterpreter::StepLse(GuestMemory& memory,
                                                 std::uint32_t op) {
    const auto pc = context.pc;
    const auto done = [&]() -> CpuResult {
        context.pc = pc + 4;
        return {CpuStop::Running, 1, 0, op};
    };
    const auto illegal = [&]() -> CpuResult {
        return {CpuStop::UndefinedInstruction, 1, pc, op};
    };
    const auto fault = [&](std::uint64_t address) -> CpuResult {
        return {CpuStop::DataFault, 1, address, op};
    };
    const unsigned size = 1U << ((op >> 30) & 3U);
    const unsigned rn = (op >> 5) & 31U;
    const unsigned rt = op & 31U;
    const unsigned rs = (op >> 16) & 31U;
    const auto address = [&] { return ReadReg(rn, true, true); };
    const auto aligned = [&](std::uint64_t a) {
        return a % size == 0 && (rn != 31U || a % 16 == 0);
    };

    // CAS{A,L,AL}{B,H} <Ws|Xs>, <Wt|Xt>, [Xn|SP].
    // CAS writes the original memory value to Rs, even on mismatch.
    // Both read and write access are required even if comparison fails.
    if ((op & 0x3FA07C00U) == 0x08A07C00U) {
        const auto a = address();
        if (!aligned(a)) return fault(a);
        const auto desired = ReadReg(rt, size == 8);
        const auto expected = ReadReg(rs, size == 8);
        const auto original = memory.AtomicRmw(
            a, size, GuestMemory::AtomicOperation::CompareSwap, desired, expected);
        if (!original) return fault(a);
        WriteReg(rs, *original, size == 8);
        return done();
    }

    // Atomic read-modify-write: LDADD, LDCLR, LDEOR, LDSET,
    // LDSMAX, LDSMIN, LDUMAX, LDUMIN, SWP and ST* aliases.
    // Rt==ZR suppresses the returned value but not the memory operation.
    if ((op & 0x3F200C00U) == 0x38200000U) {
        const unsigned selector = (op >> 12) & 7U;
        const bool swap = (op & (1U << 15)) != 0;
        if (swap && selector != 0) return illegal();
        const auto kind = swap ? GuestMemory::AtomicOperation::Swap :
            static_cast<GuestMemory::AtomicOperation>(selector + 1);
        const auto a = address();
        if (!aligned(a)) return fault(a);
        const auto input = ReadReg(rs, size == 8);
        const auto original = memory.AtomicRmw(a, size, kind, input);
        if (!original) return fault(a);
        WriteReg(rt, *original, size == 8);
        return done();
    }

    // Non-exclusive LDAR/STLR byte, halfword, word, doubleword forms.
    // All are single-copy atomic in this cooperative reference runtime.
    const auto ordered = op & 0x3FFFFC00U;
    if (ordered == 0x08DFFC00U || ordered == 0x089FFC00U) {
        const auto a = address();
        if (!aligned(a)) return fault(a);
        if (ordered == 0x08DFFC00U) {
            std::optional<std::uint64_t> value;
            if (size == 1) { if (auto v = memory.Read8(a)) value = *v; }
            if (size == 2) { if (auto v = memory.Read16(a)) value = *v; }
            if (size == 4) { if (auto v = memory.Read32(a)) value = *v; }
            if (size == 8) value = memory.Read64(a);
            if (!value) return fault(a);
            WriteReg(rt, *value, size == 8);
        } else {
            const auto value = ReadReg(rt, size == 8);
            bool ok = false;
            if (size == 1) ok = memory.Write8(a, static_cast<std::uint8_t>(value));
            if (size == 2) ok = memory.Write16(a, static_cast<std::uint16_t>(value));
            if (size == 4) ok = memory.Write32(a, static_cast<std::uint32_t>(value));
            if (size == 8) ok = memory.Write64(a, value);
            if (!ok) return fault(a);
        }
        return done();
    }
    return std::nullopt;
}
} // namespace nint2
