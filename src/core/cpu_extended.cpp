#include "nint2/cpu.hpp"
#include <array>
#include <bit>
#include <limits>
#include <cstdint>
#include <optional>
#include <span>

namespace nint2 {
namespace {
[[nodiscard]] constexpr std::uint64_t LowMask(unsigned bits) noexcept {
    return bits == 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
}
[[nodiscard]] std::optional<std::uint64_t> DecodeLogicalImmediate(std::uint32_t op,
                                                                  bool wide) noexcept {
    // Arm A64 logical-immediate bitmask: a rotated run of ones replicated to
    // 32 or 64 bits. Invalid all-ones and zero-element encodings must trap.
    const unsigned n = (op >> 22) & 1U;
    const unsigned immr = (op >> 16) & 63U;
    const unsigned imms = (op >> 10) & 63U;
    if (!wide && n != 0) return std::nullopt;
    const unsigned lengths = (n << 6) | ((~imms) & 63U);
    unsigned len = 0;
    for (unsigned bit = 6; bit > 0; --bit) {
        if (lengths & (1U << bit)) { len = bit; break; }
    }
    if (len == 0) return std::nullopt;
    const unsigned element_bits = 1U << len;
    const unsigned width = wide ? 64U : 32U;
    if (element_bits > width) return std::nullopt;
    const unsigned levels = element_bits - 1U;
    const unsigned s = imms & levels;
    const unsigned r = immr & levels;
    if (s == levels) return std::nullopt;
    const auto field = LowMask(s + 1);
    // Both shift counts are guaranteed below 64, including r == 0.
    const auto element = ((field >> r) |
                          (field << ((element_bits - r) & levels))) &
                         LowMask(element_bits);
    std::uint64_t mask = 0;
    for (unsigned offset = 0; offset < width; offset += element_bits)
        mask |= element << offset;
    return mask;
}
[[nodiscard]] constexpr std::int64_t SignedImmediate(std::uint64_t value, unsigned bits) noexcept {
    const auto mask = LowMask(bits);
    value &= mask;
    if (value & (std::uint64_t{1} << (bits - 1)))
        return -static_cast<std::int64_t>((~value & mask) + 1);
    return static_cast<std::int64_t>(value);
}
[[nodiscard]] std::optional<std::uint64_t> Load(GuestMemory& mem, std::uint64_t addr,
                                                unsigned bytes) {
    switch (bytes) {
    case 1: if (const auto x = mem.Read8(addr)) return *x; break;
    case 2: if (const auto x = mem.Read16(addr)) return *x; break;
    case 4: if (const auto x = mem.Read32(addr)) return *x; break;
    case 8: return mem.Read64(addr);
    default: break;
    }
    return std::nullopt;
}
[[nodiscard]] bool Store(GuestMemory& mem, std::uint64_t addr,
                         std::uint64_t value, unsigned bytes) {
    switch (bytes) {
    case 1: return mem.Write8(addr, static_cast<std::uint8_t>(value));
    case 2: return mem.Write16(addr, static_cast<std::uint16_t>(value));
    case 4: return mem.Write32(addr, static_cast<std::uint32_t>(value));
    case 8: return mem.Write64(addr, value);
    default: return false;
    }
}
} // namespace

// Supplemental, explicitly scoped A64 integer encodings. The main interpreter
// only calls this for instructions outside its original narrow decoder.
std::optional<CpuResult> CpuInterpreter::StepExtended(GuestMemory& memory, std::uint32_t op) {
    const auto pc = context.pc;
    const unsigned rt = op & 31;
    const unsigned rn = (op >> 5) & 31;
    const bool wide = (op >> 31) != 0;
    const auto illegal = [&] { return CpuResult{CpuStop::UndefinedInstruction, 1, pc, op}; };
    const auto fault = [&](std::uint64_t address) {
        return CpuResult{CpuStop::DataFault, 1, address, op};
    };
    const auto done = [&] {
        context.pc = pc + 4;
        return CpuResult{CpuStop::Running, 1, 0, op};
    };

    if ((op & 0x7FA00000U) == 0x13800000U) {
        // EXTR and ROR (immediate alias): select a width-bit window from
        // Rn:Rm. A64 requires N == sf; W-form shift must be less than 32.
        const unsigned width = wide ? 64U : 32U;
        const unsigned n = (op >> 22) & 1U;
        const unsigned shift = (op >> 10) & 63U;
        if (n != static_cast<unsigned>(wide) || shift >= width)
            return illegal();
        const auto high = ReadReg(rn, wide);
        const auto low = ReadReg((op >> 16) & 31U, wide);
        const auto result = shift == 0 ? low
            : ((low >> shift) | (high << (width - shift)));
        WriteReg(rt, result, wide);
        return done();
    }
    if ((op & 0x1FE0FC00U) == 0x1A000000U) {
        // ADC/ADCS/SBC/SBCS (and NGC/NGCS aliases). Guest C flag means
        // carry for addition and *no borrow* for subtraction. In particular,
        // a carry-in may generate an unsigned carry even when a+b did not.
        const bool subtract = (op & (1U << 30)) != 0;
        const bool set_flags = (op & (1U << 29)) != 0;
        const bool carry_in = (context.nzcv & 2U) != 0;
        const auto a = ReadReg(rn, wide);
        const auto b = ReadReg((op >> 16) & 31U, wide);
        const auto mask = wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
        const auto sign = wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31);
        const auto borrow = carry_in ? std::uint64_t{0} : std::uint64_t{1};
        const auto sum = a + b;
        const auto value = (subtract ? (a - b - borrow)
                                     : (sum + (carry_in ? 1U : 0U))) & mask;
        if (set_flags) {
            const bool carry_out = subtract
                ? (carry_in ? a >= b : a > b)
                : (wide ? (sum < a || (carry_in && sum == UINT64_MAX))
                        : (sum + static_cast<unsigned>(carry_in) > UINT32_MAX));
            const bool overflow = subtract
                ? (((a ^ b) & (a ^ value) & sign) != 0)
                : (((~(a ^ b)) & (a ^ value) & sign) != 0);
            context.nzcv = static_cast<std::uint8_t>(
                ((value & sign) ? 8 : 0) | (value == 0 ? 4 : 0) |
                (carry_out ? 2 : 0) | (overflow ? 1 : 0));
        }
        WriteReg(rt, value, wide);
        return done();
    }
    if ((op & 0x1F800000U) == 0x12000000U) {
        // AND/ORR/EOR/ANDS (immediate), including MOV/TST aliases.
        const auto bitmask = DecodeLogicalImmediate(op, wide);
        if (!bitmask) return illegal();
        const auto a = ReadReg(rn, wide);
        const unsigned operation = (op >> 29) & 3U;
        const auto value = operation == 0 ? (a & *bitmask) :
                           operation == 1 ? (a | *bitmask) :
                           operation == 2 ? (a ^ *bitmask) : (a & *bitmask);
        // AND/ORR/EOR immediate may write SP; ANDS/TST writes ZR instead.
        // Rn==31 remains the zero register in all logical-immediate forms.
        WriteReg(rt, value, wide, operation != 3);
        if (operation == 3) {
            const auto width_mask = wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
            const auto sign = wide ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 31);
            const auto truncated = value & width_mask;
            context.nzcv = static_cast<std::uint8_t>(((truncated & sign) ? 8 : 0) |
                                                     (truncated == 0 ? 4 : 0));
        }
        return done();
    }
    if ((op & 0x7FE00000U) == 0x1AC00000U) {
        // A64 data-processing (2-source): unsigned/signed division and
        // variable shifts. Unsupported opcodes in this family trap rather than
        // accidentally executing as another ALU instruction.
        const unsigned opcode = (op >> 10) & 63U;
        if (opcode != 2 && opcode != 3 &&
            opcode != 8 && opcode != 9 && opcode != 10 && opcode != 11)
            return illegal();
        const unsigned width = wide ? 64U : 32U;
        const std::uint64_t mask = wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
        const auto a = ReadReg(rn, wide);
        const auto b = ReadReg((op >> 16) & 31U, wide);
        std::uint64_t value = 0;
        if (opcode == 2) { // UDIV: ARM defines division by zero as zero.
            value = b == 0 ? 0 : a / b;
        } else if (opcode == 3) { // SDIV, including INT_MIN / -1.
            if (wide) {
                const auto numerator = std::bit_cast<std::int64_t>(a);
                const auto divisor = std::bit_cast<std::int64_t>(b);
                if (divisor == 0) value = 0;
                else if (numerator == std::numeric_limits<std::int64_t>::min() &&
                         divisor == -1) value = a; // architecturally wraps
                else value = static_cast<std::uint64_t>(numerator / divisor);
            } else {
                const auto numerator =
                    std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a));
                const auto divisor =
                    std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(b));
                if (divisor == 0) value = 0;
                else if (numerator == std::numeric_limits<std::int32_t>::min() &&
                         divisor == -1) value = a;
                else value = static_cast<std::uint32_t>(numerator / divisor);
            }
        } else {
            // ARM variable shifts ignore high shift-count bits (modulo width).
            const unsigned shift = static_cast<unsigned>(b & (width - 1));
            if (opcode == 8) value = (a << shift) & mask; // LSLV
            if (opcode == 9) value = a >> shift; // LSRV
            if (opcode == 10) { // ASRV, explicitly portable sign-fill.
                value = a >> shift;
                const std::uint64_t sign = std::uint64_t{1} << (width - 1);
                if (shift != 0 && (a & sign))
                    value |= mask & ~(mask >> shift);
            }
            if (opcode == 11) { // RORV; never shift by the word width.
                value = shift == 0 ? a :
                        ((a >> shift) | (a << (width - shift))) & mask;
            }
        }
        WriteReg(rt, value, wide);
        return done();
    }
    if ((op & 0x7E000000U) == 0x36000000U) {
        // TBZ/TBNZ: 14-bit PC-relative bit-test branches, W/X per bit number.
        const unsigned bit = ((op >> 26) & 0x20U) | ((op >> 19) & 31U);
        const bool not_zero = (op & (1U << 24)) != 0;
        const bool bit_set = (ReadReg(rt, bit >= 32) & (std::uint64_t{1} << bit)) != 0;
        const auto delta = SignedImmediate((op >> 5) & 0x3fff, 14) * 4;
        context.pc = bit_set == not_zero ? pc + static_cast<std::uint64_t>(delta) : pc + 4;
        return CpuResult{CpuStop::Running, 1, 0, op};
    }
    if ((op & 0x3A000000U) == 0x28000000U) {
        // STP/LDP integer pair, signed offset/postindex/preindex.
        // Exclude SIMD, LDPSW and non-temporal pair variants.
        const unsigned opc = (op >> 30) & 3;
        const unsigned mode = (op >> 23) & 3;
        if ((op & (1U << 26)) || (opc != 0 && opc != 2) || mode == 0)
            return illegal();
        const unsigned bytes = opc == 2 ? 8 : 4;
        const bool load = (op & (1U << 22)) != 0;
        const unsigned rt2 = (op >> 10) & 31;
        const bool writeback = mode != 2;
        if ((load && rt == rt2) || (writeback && rn != 31 && (rn == rt || rn == rt2)))
            return illegal(); // constrained-unpredictable forms are rejected
        const auto base = ReadReg(rn, true, true);
        const auto offset = SignedImmediate((op >> 15) & 0x7f, 7) * bytes;
        const auto address = base + (mode == 1 ? 0 : static_cast<std::uint64_t>(offset));
        const auto second = address + bytes;
        if (load) {
            const auto first_value = Load(memory, address, bytes);
            if (!first_value) return fault(address);
            const auto second_value = Load(memory, second, bytes);
            if (!second_value) return fault(second);
            WriteReg(rt, *first_value, bytes == 8);
            WriteReg(rt2, *second_value, bytes == 8);
        } else {
            std::array<std::uint8_t, 16> buffer{};
            const auto first_value = ReadReg(rt, bytes == 8);
            const auto second_value = ReadReg(rt2, bytes == 8);
            for (unsigned i = 0; i < bytes; ++i) {
                buffer[i] = static_cast<std::uint8_t>(first_value >> (8 * i));
                buffer[bytes + i] = static_cast<std::uint8_t>(second_value >> (8 * i));
            }
            if (!memory.WriteBuffer(address, std::span<const std::uint8_t>(buffer.data(), 2 * bytes)))
                return fault(address);
        }
        if (writeback) WriteReg(rn, base + static_cast<std::uint64_t>(offset), true, true);
        return done();
    }
    if ((op & 0x3B000000U) == 0x39000000U && ((op >> 22) & 3) >= 2) {
        // LDRSB/LDRSH/LDRSW unsigned scaled offsets. Opc==2 extends to X,
        // opc==3 extends an 8/16-bit value to W (zeroing X upper bits).
        const unsigned opc = (op >> 22) & 3;
        const unsigned size = (op >> 30) & 3;
        const unsigned bytes = 1U << size;
        if ((op & (1U << 26)) || bytes == 8 || (opc == 3 && bytes > 2))
            return illegal();
        const auto address = ReadReg(rn, true, true) +
                             (std::uint64_t{(op >> 10) & 4095} << size);
        const auto value = Load(memory, address, bytes);
        if (!value) return fault(address);
        WriteReg(rt, static_cast<std::uint64_t>(SignedImmediate(*value, bytes * 8)),
                 opc == 2);
        return done();
    }
    if ((op & 0x3B200000U) == 0x38000000U) {
        // STUR/LDUR, plus ST(R)/LD(R) post-index and pre-index forms.
        const unsigned opc = (op >> 22) & 3;
        const unsigned mode = (op >> 10) & 3;
        const unsigned bytes = 1U << ((op >> 30) & 3);
        if ((op & (1U << 26)) || mode == 2 ||
            (opc >= 2 && (bytes == 8 || (opc == 3 && bytes > 2))))
            return illegal();
        const bool writeback = mode != 0;
        if (writeback && rn != 31 && rn == rt) return illegal();
        const auto base = ReadReg(rn, true, true);
        const auto offset = SignedImmediate((op >> 12) & 0x1ff, 9);
        const auto address = base + (mode == 1 ? 0 : static_cast<std::uint64_t>(offset));
        if (opc != 0) {
            const auto value = Load(memory, address, bytes);
            if (!value) return fault(address);
            WriteReg(rt, opc >= 2 ?
                     static_cast<std::uint64_t>(SignedImmediate(*value, bytes * 8)) : *value,
                     opc == 2 || (opc == 1 && bytes == 8));
        } else if (!Store(memory, address, ReadReg(rt, bytes == 8), bytes)) {
            return fault(address);
        }
        if (writeback) WriteReg(rn, base + static_cast<std::uint64_t>(offset), true, true);
        return done();
    }
    if ((op & 0x3B200C00U) == 0x38200800U) {
        // Scalar register-offset LDR/STR: UXTW/LSL/SXTW/SXTX offsets.
        const unsigned opc = (op >> 22) & 3;
        const unsigned option = (op >> 13) & 7;
        if ((op & (1U << 26)) ||
            (option != 2 && option != 3 && option != 6 && option != 7)) return illegal();
        const unsigned sz = (op >> 30) & 3;
        const unsigned bytes = 1U << sz;
        if (opc >= 2 && (bytes == 8 || (opc == 3 && bytes > 2))) return illegal();
        const unsigned rm = (op >> 16) & 31;
        const auto raw = ReadReg(rm, option == 3 || option == 7);
        std::uint64_t offset = raw;
        if (option == 6)
            offset = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(raw)));
        const unsigned scale = (op & (1U << 12)) ? sz : 0;
        const auto address = ReadReg(rn, true, true) + (offset << scale);
        if (opc != 0) {
            const auto value = Load(memory, address, bytes);
            if (!value) return fault(address);
            WriteReg(rt, opc >= 2 ?
                     static_cast<std::uint64_t>(SignedImmediate(*value, bytes * 8)) : *value,
                     opc == 2 || (opc == 1 && bytes == 8));
        } else if (!Store(memory, address, ReadReg(rt, bytes == 8), bytes)) {
            return fault(address);
        }
        return done();
    }
    if ((op & 0x3B000000U) == 0x18000000U) {
        // LDR W/X literal and LDRSW literal; not PRFM or SIMD.
        const unsigned opc = (op >> 30) & 3;
        if ((op & (1U << 26)) || opc == 3) return illegal();
        const auto displacement = SignedImmediate((op >> 5) & 0x7ffff, 19) * 4;
        const auto address = pc + static_cast<std::uint64_t>(displacement);
        const auto value = Load(memory, address, opc == 1 ? 8 : 4);
        if (!value) return fault(address);
        if (opc == 2)
            WriteReg(rt, static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(*value))), true);
        else WriteReg(rt, *value, opc == 1);
        return done();
    }
    if ((op & 0x1FE00000U) == 0x1A800000U) {
        // CSEL, CSINC, CSINV, CSNEG (includes CSET/CSETM aliases).
        if (op & (1U << 11)) return illegal();
        const unsigned cond = (op >> 12) & 15;
        if (cond == 15) return illegal();
        const auto a = ReadReg(rn, wide);
        const auto b = ReadReg((op >> 16) & 31, wide);
        std::uint64_t chosen = a;
        if (!Condition(cond)) {
            const bool negate_or_invert = (op & (1U << 30)) != 0;
            const bool increment = (op & (1U << 10)) != 0;
            chosen = negate_or_invert ? ~b : b;
            if (increment) ++chosen;
        }
        WriteReg(rt, chosen, wide);
        return done();
    }
    if ((op & 0x1FE00000U) == 0x1B000000U) {
        // MADD/MSUB including MUL/MNEG aliases. Unsigned modular arithmetic.
        const auto mul = ReadReg(rn, wide) * ReadReg((op >> 16) & 31, wide);
        const auto acc = ReadReg((op >> 10) & 31, wide);
        WriteReg(rt, (op & (1U << 15)) ? acc - mul : acc + mul, wide);
        return done();
    }
    const auto family = op & 0x7F800000U;
    if (family == 0x13000000U || family == 0x53000000U) {
        // Signed/unsigned bitfield move (SBFM/UBFM) including SXTW and
        // compiler-produced ASR/LSR/LSL and signed bitfield insert aliases.
        const bool is_signed = family == 0x13000000U;
        const unsigned immr = (op >> 16) & 63;
        const unsigned imms = (op >> 10) & 63;
        const bool n = (op & (1U << 22)) != 0;
        const unsigned width = wide ? 64 : 32;
        if (n != wide || immr >= width || imms >= width) return illegal();
        const auto source = ReadReg(rn, wide);
        std::uint64_t value;
        unsigned field_bits;
        if (immr <= imms) {
            field_bits = imms - immr + 1;
            value = (source >> immr) & LowMask(field_bits);
        } else {
            const unsigned shift = width - immr;
            field_bits = imms + 1;
            value = (source & LowMask(field_bits)) << shift;
            field_bits += shift;
        }
        if (is_signed && (value & (std::uint64_t{1} << (field_bits - 1))))
            value |= ~LowMask(field_bits);
        WriteReg(rt, value, wide);
        return done();
    }
    if ((op & 0x7FE00000U) == 0x5AC00000U) {
        // A64 data-processing (one-source): RBIT, REV16, REV32/REV,
        // CLZ and CLS. Only the allocated scalar W/X encodings are accepted.
        // Bits [20:16] are reserved in this family.
        const unsigned width = wide ? 64 : 32;
        const unsigned opcode = (op >> 10) & 63;
        if (((op >> 16) & 31) != 0 || opcode > 5 ||
            (!wide && opcode == 3)) return illegal();
        const auto value = ReadReg(rn, wide);
        std::uint64_t output = 0;
        if (opcode == 0) {
            // Bit reversal, not a byte reversal.
            for (unsigned i = 0; i < width; ++i)
                output |= ((value >> i) & 1ULL) << (width - 1 - i);
        } else if (opcode <= 3) {
            // Reverse bytes independently inside 16-, 32-, or 64-bit groups.
            const unsigned group_bytes = opcode == 1 ? 2 : opcode == 2 ? 4 : 8;
            const unsigned bytes = width / 8;
            for (unsigned i = 0; i < bytes; ++i) {
                const unsigned group = i / group_bytes;
                const unsigned reversed = group * group_bytes +
                                          (group_bytes - 1 - i % group_bytes);
                output |= ((value >> (8 * i)) & 0xffULL) << (8 * reversed);
            }
        } else if (opcode == 4) {
            // CLZ(0) is defined: 32 for WZR, 64 for XZR.
            unsigned count = 0;
            for (unsigned bit = width; bit > 0; --bit) {
                if ((value >> (bit - 1)) & 1U) break;
                ++count;
            }
            output = count;
        } else {
            // CLS counts leading sign bits EXCLUDING the most significant bit.
            const auto sign = (value >> (width - 1)) & 1U;
            unsigned count = 0;
            for (unsigned bit = width - 1; bit > 0; --bit) {
                if (((value >> (bit - 1)) & 1U) != sign) break;
                ++count;
            }
            output = count;
        }
        WriteReg(rt, output, wide);
        return done();
    }
    return std::nullopt;
}
} // namespace nint2
