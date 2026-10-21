#include "nint2/cpu.hpp"
#include <cstdint>
#include <limits>

namespace nint2 {
namespace {
[[nodiscard]] std::int64_t SignExtend(std::uint64_t value, unsigned bits) {
    const std::uint64_t sign = std::uint64_t{1} << (bits - 1);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}
[[nodiscard]] std::uint64_t Shift(std::uint64_t value, unsigned shift, unsigned kind,
                                  bool wide) {
    const std::uint64_t mask = wide ? UINT64_MAX : UINT32_MAX;
    value &= mask;
    if (kind == 0) return (value << shift) & mask;
    if (kind == 1) return value >> shift;
    if (kind == 2) {
        if (wide) return static_cast<std::uint64_t>(static_cast<std::int64_t>(value) >> shift);
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> shift);
    }
    // Logical shifted-register instructions also accept ROR. The zero case
    // must not shift left by the register width (undefined in host C++).
    const unsigned width = wide ? 64 : 32;
    return shift == 0 ? value : ((value >> shift) | (value << (width - shift))) & mask;
}
} // namespace

std::uint64_t CpuInterpreter::ReadReg(unsigned index, bool wide, bool use_sp) const {
    const std::uint64_t value = index == 31 ? (use_sp ? context.sp : 0) : context.x[index];
    return wide ? value : static_cast<std::uint32_t>(value);
}
void CpuInterpreter::WriteReg(unsigned index, std::uint64_t value, bool wide, bool use_sp) {
    if (!wide) value = static_cast<std::uint32_t>(value);
    if (index == 31) {
        if (use_sp) context.sp = value;
    } else {
        context.x[index] = value;
    }
}
void CpuInterpreter::SetArithmeticFlags(std::uint64_t a, std::uint64_t b,
                                         std::uint64_t result, bool wide, bool subtraction) {
    const auto mask = wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
    const auto sign = wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31);
    a &= mask; b &= mask; result &= mask;
    const bool negative = (result & sign) != 0;
    const bool zero = result == 0;
    const bool carry = subtraction ? a >= b : (result & mask) < a;
    const bool overflow = subtraction ? ((a ^ b) & (a ^ result) & sign) != 0
                                      : ((~(a ^ b)) & (a ^ result) & sign) != 0;
    context.nzcv = static_cast<std::uint8_t>((negative ? 8 : 0) | (zero ? 4 : 0) |
                                             (carry ? 2 : 0) | (overflow ? 1 : 0));
}
bool CpuInterpreter::Condition(unsigned cond) const {
    const bool n = (context.nzcv & 8) != 0, z = (context.nzcv & 4) != 0;
    const bool c = (context.nzcv & 2) != 0, v = (context.nzcv & 1) != 0;
    switch (cond) {
    case 0: return z;
    case 1: return !z;
    case 2: return c;
    case 3: return !c;
    case 4: return n;
    case 5: return !n;
    case 6: return v;
    case 7: return !v;
    case 8: return c && !z;
    case 9: return !c || z;
    case 10: return n == v;
    case 11: return n != v;
    case 12: return !z && n == v;
    case 13: return z || n != v;
    case 14: return true;
    default: return false;
    }
}

CpuResult CpuInterpreter::Step(GuestMemory& memory) {
    const auto pc = context.pc;
    const auto instruction = memory.Fetch32(pc);
    if (!instruction) return {CpuStop::InstructionFault, 1, pc, 0};
    const std::uint32_t op = *instruction;
    auto result = CpuResult{CpuStop::Running, 1, 0, op};
    std::uint64_t next = pc + 4;
    const bool wide = (op >> 31) != 0;
    const unsigned rd = op & 31;
    const unsigned rn = (op >> 5) & 31;

    // Hints: only the architecturally defined NOP is supported.
    if (op == 0xD503201FU) {
        // NOP
    } else if ((op & 0xFFE0001FU) == 0xD4200000U) {
        result.stop = CpuStop::Breakpoint;
        return result; // leave PC on the trapping instruction
    } else if ((op & 0xFFE0001FU) == 0xD4000001U) {
        result.stop = CpuStop::SupervisorCall;
        return result;
    } else if ((op & 0x1F000000U) == 0x10000000U) {
        // ADR / ADRP: signed 21-bit PC-relative byte/page address generation.
        const std::uint64_t encoded = (((op >> 5) & 0x7ffffULL) << 2) | ((op >> 29) & 3);
        const auto delta = SignExtend(encoded, 21);
        const bool page = (op & 0x80000000U) != 0;
        const auto base = page ? (pc & ~std::uint64_t{4095}) : pc;
        const auto scaled = delta * (page ? 4096 : 1);
        WriteReg(rd, base + static_cast<std::uint64_t>(scaled), true);
    } else if ((op & 0x7F800000U) == 0x52800000U ||
               (op & 0x7F800000U) == 0x72800000U ||
               (op & 0x7F800000U) == 0x12800000U) {
        // MOVZ/MOVK/MOVN. The 32-bit form may use only two halfwords.
        const unsigned hw = (op >> 21) & 3;
        if (!wide && hw > 1) return {CpuStop::UndefinedInstruction, 1, pc, op};
        const unsigned shift = hw * 16;
        const std::uint64_t immediate = std::uint64_t{(op >> 5) & 0xffff} << shift;
        const auto family = op & 0x7F800000U;
        std::uint64_t value = 0;
        if (family == 0x52800000U) value = immediate;
        else if (family == 0x12800000U) value = ~immediate;
        else value = (ReadReg(rd, wide) & ~(std::uint64_t{0xffff} << shift)) | immediate;
        WriteReg(rd, value, wide);
    } else if ((op & 0x1F000000U) == 0x11000000U) {
        // ADD/SUB/ADDS/SUBS (immediate), including SP aliases without flags.
        const unsigned sh = (op >> 22) & 3;
        if (sh > 1) return {CpuStop::UndefinedInstruction, 1, pc, op};
        const bool sub = (op & (1U << 30)) != 0;
        const bool flags = (op & (1U << 29)) != 0;
        // Rn==31 is SP regardless of S. Only the destination changes to ZR
        // for ADDS/SUBS (including CMN/CMP), so comparing SP reads its value.
        const auto a = ReadReg(rn, wide, true);
        const std::uint64_t b = std::uint64_t{(op >> 10) & 4095} << (12 * sh);
        const std::uint64_t value = sub ? a - b : a + b;
        if (flags) SetArithmeticFlags(a, b, value, wide, sub);
        WriteReg(rd, value, wide, !flags);
    } else if ((op & 0x1F200000U) == 0x0B000000U ||
               (op & 0x1F000000U) == 0x0A000000U) {
        // Integer shifted-register arithmetic and logical ops, no extensions.
        const bool arithmetic = (op & 0x01000000U) != 0;
        const unsigned kind = (op >> 22) & 3;
        const unsigned shift = (op >> 10) & 63;
        if ((arithmetic && kind == 3) || (!wide && shift >= 32))
            return {CpuStop::UndefinedInstruction, 1, pc, op};
        const auto a = ReadReg(rn, wide);
        const auto shifted = Shift(ReadReg((op >> 16) & 31, wide), shift, kind, wide);
        // Logical (shifted register) uses bit 21 to invert operand two:
        // BIC/ORN/EON/BICS are the inverted forms of AND/ORR/EOR/ANDS.
        const auto b = (!arithmetic && (op & (1U << 21))) ? ~shifted : shifted;
        if (arithmetic) {
            const bool sub = (op & (1U << 30)) != 0;
            const bool flags = (op & (1U << 29)) != 0;
            const auto value = sub ? a - b : a + b;
            if (flags) SetArithmeticFlags(a, b, value, wide, sub);
            WriteReg(rd, value, wide);
        } else {
            const unsigned which = (op >> 29) & 3;
            const auto value = which == 0 ? (a & b) : which == 1 ? (a | b) :
                               which == 2 ? (a ^ b) : (a & b);
            WriteReg(rd, value, wide);
            if (which == 3) {
                const auto mask = wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
                const auto sign = wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31);
                const auto v = value & mask;
                context.nzcv = static_cast<std::uint8_t>(((v & sign) ? 8 : 0) |
                                                         (v == 0 ? 4 : 0));
            }
        }
    } else if ((op & 0x3B000000U) == 0x39000000U &&
               (op & (1U << 26)) == 0 && ((op >> 22) & 3) <= 1) {
        // Unsigned-offset LDR/STR (8,16,32,64 bits), base register may be SP.
        const unsigned sz = (op >> 30) & 3;
        const unsigned bytes = 1U << sz;
        const auto address = ReadReg(rn, true, true) +
                             (std::uint64_t{(op >> 10) & 4095} << sz);
        const bool load = (op & (1U << 22)) != 0;
        if (load) {
            std::optional<std::uint64_t> value;
            if (bytes == 1) { if (auto v = memory.Read8(address)) value = *v; }
            if (bytes == 2) { if (auto v = memory.Read16(address)) value = *v; }
            if (bytes == 4) { if (auto v = memory.Read32(address)) value = *v; }
            if (bytes == 8) value = memory.Read64(address);
            if (!value) return {CpuStop::DataFault, 1, address, op};
            WriteReg(rd, *value, bytes == 8);
        } else {
            const auto value = ReadReg(rd, bytes == 8);
            bool ok = false;
            if (bytes == 1) ok = memory.Write8(address, static_cast<std::uint8_t>(value));
            if (bytes == 2) ok = memory.Write16(address, static_cast<std::uint16_t>(value));
            if (bytes == 4) ok = memory.Write32(address, static_cast<std::uint32_t>(value));
            if (bytes == 8) ok = memory.Write64(address, value);
            if (!ok) return {CpuStop::DataFault, 1, address, op};
        }
    } else if ((op & 0x7C000000U) == 0x14000000U) {
        // B and BL (PC-relative 26-bit signed immediate).
        if (op & 0x80000000U) context.x[30] = next;
        next = pc + static_cast<std::uint64_t>(SignExtend(op & 0x03ffffff, 26) * 4);
    } else if ((op & 0x7E000000U) == 0x34000000U) {
        // CBZ/CBNZ in W or X mode.
        const bool not_zero = (op & (1U << 24)) != 0;
        if ((ReadReg(rd, wide) != 0) == not_zero)
            next = pc + static_cast<std::uint64_t>(SignExtend((op >> 5) & 0x7ffff, 19) * 4);
    } else if ((op & 0xFF000010U) == 0x54000000U) {
        // B.cond (19-bit signed PC-relative offset).
        const unsigned cond = op & 15;
        if (cond == 15) return {CpuStop::UndefinedInstruction, 1, pc, op};
        if (Condition(cond))
            next = pc + static_cast<std::uint64_t>(SignExtend((op >> 5) & 0x7ffff, 19) * 4);
    } else if ((op & 0xFFFFFC1FU) == 0xD61F0000U ||
               (op & 0xFFFFFC1FU) == 0xD63F0000U ||
               (op & 0xFFFFFC1FU) == 0xD65F0000U) {
        const auto kind = op & 0xFFFFFC1FU;
        const auto target = ReadReg(rn, true);
        if (kind == 0xD63F0000U) context.x[30] = next; // BLR
        next = target;
    } else {
        if (const auto atomic = StepAtomic(memory, op)) return *atomic;
        if (const auto system = StepSystem(op)) return *system;
        if (const auto extended = StepExtended(memory, op)) return *extended;
        return {CpuStop::UndefinedInstruction, 1, pc, op};
    }
    context.pc = next;
    return result;
}

CpuResult CpuInterpreter::Run(GuestMemory& memory, std::uint64_t max_steps) {
    for (std::uint64_t i = 0; i < max_steps; ++i) {
        auto result = Step(memory);
        if (result.stop != CpuStop::Running) {
            result.steps = i + 1;
            return result;
        }
    }
    return {CpuStop::StepLimit, max_steps, context.pc, 0};
}
} // namespace nint2
