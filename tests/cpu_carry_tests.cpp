#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "nint2/cpu.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
struct Oracle {
    std::uint64_t value{};
    std::uint8_t flags{};
};
Oracle Reference(std::uint64_t input_a, std::uint64_t input_b,
                 bool wide, bool subtract, bool carry) {
    const std::uint64_t mask = wide ? UINT64_MAX : UINT32_MAX;
    const unsigned width = wide ? 64 : 32;
    const auto a = input_a & mask, b = input_b & mask;
    const auto result = (subtract ? (a - b - (carry ? 0U : 1U))
                                  : (a + b + (carry ? 1U : 0U))) & mask;
    const bool sign_a = (a >> (width - 1)) != 0;
    const bool sign_b = (b >> (width - 1)) != 0;
    const bool sign_r = (result >> (width - 1)) != 0;
    const bool overflow = subtract ? (sign_a != sign_b && sign_r != sign_a)
                                   : (sign_a == sign_b && sign_r != sign_a);
    // Separate oracle using unsigned comparisons instead of host signed
    // division, wider-than-native temporaries or undefined overflow.
    const bool carry_out = subtract ? (carry ? a >= b : a > b)
        : wide ? (a + b < a || (carry && a + b == UINT64_MAX))
               : ((a + b + (carry ? 1U : 0U)) >> 32) != 0;
    return {result, static_cast<std::uint8_t>(
        (sign_r ? 8 : 0) | (result == 0 ? 4 : 0) |
        (carry_out ? 2 : 0) | (overflow ? 1 : 0))};
}
} // namespace

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    // Machine words independently assembled from original_carry.s (Clang 17).
    constexpr std::array<std::uint32_t, 11> code = {
        0x9a020020, 0x3a050083, 0xda0800e6, 0x7a0b0149,
        0xda0d03ec, 0x7a0f03ee, 0x1a1103f0, 0xba140272,
        0xfa1702d5, 0xd65f03c0, 0x9a020420 // final encoding reserved
    };
    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 4096, Access::Read | Access::Write));
    for (std::size_t i = 0; i < code.size(); ++i)
        EXPECT(memory.Write32(0x1000 + i * 4, code[i]));
    EXPECT(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    auto& x = cpu.context.x;
    x[1] = 5; x[2] = 7;
    x[4] = UINT32_MAX; x[5] = 0;
    x[7] = 20; x[8] = 7;
    x[10] = 0; x[11] = 1;
    x[13] = 4; x[15] = 0; x[17] = 31;
    x[19] = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    x[20] = 1;
    x[22] = std::uint64_t{1} << 63; x[23] = 1;
    cpu.context.nzcv = 2; // C=1
    for (unsigned i = 0; i < 9; ++i) {
        cpu.context.pc = 0x1000 + 4 * i;
        EXPECT(cpu.Step(memory).stop == CpuStop::Running);
        EXPECT(cpu.context.pc == 0x1000 + 4 * (i + 1));
        if (i == 0) EXPECT(x[0] == 13 && cpu.context.nzcv == 2);
        if (i == 1) EXPECT(x[3] == 0 && cpu.context.nzcv == 6);
        if (i == 2) EXPECT(x[6] == 13 && cpu.context.nzcv == 6);
        if (i == 3) EXPECT(x[9] == UINT32_MAX && cpu.context.nzcv == 8);
        if (i == 4) EXPECT(x[12] == UINT64_MAX - 4 && cpu.context.nzcv == 8);
        if (i == 5) EXPECT(x[14] == UINT32_MAX && cpu.context.nzcv == 8);
        if (i == 6) EXPECT(x[16] == 31 && cpu.context.nzcv == 8);
        if (i == 7) EXPECT(x[18] == (std::uint64_t{1} << 63) &&
                             cpu.context.nzcv == 9);
        if (i == 8) EXPECT(x[21] == ((std::uint64_t{1} << 63) - 2) &&
                             cpu.context.nzcv == 3);
    }

    // 1024 deterministic vector sets, each checking ADC/ADCS/SBC/SBCS in
    // W and X forms, with both carry-in states and flag preservation.
    std::uint64_t seed = 0xacaeca1122334455ULL;
    for (unsigned i = 0; i < 1024; ++i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto a = seed;
        seed = seed * 2862933555777941757ULL + 3037000493ULL;
        const auto b = seed;
        for (bool carry : {false, true}) {
            struct Case { unsigned index, dest, src_a, src_b; bool wide, sub, flags; };
            constexpr std::array<Case, 6> cases{{
                {0, 0, 1, 2, true, false, false},
                {1, 3, 4, 5, false, false, true},
                {2, 6, 7, 8, true, true, false},
                {3, 9, 10, 11, false, true, true},
                {7, 18, 19, 20, true, false, true},
                {8, 21, 22, 23, true, true, true}
            }};
            for (const auto& test : cases) {
                x[test.src_a] = a; x[test.src_b] = b;
                cpu.context.nzcv = static_cast<std::uint8_t>(8 | (carry ? 2 : 0));
                cpu.context.pc = 0x1000 + 4 * test.index;
                const auto outcome = Reference(a, b, test.wide, test.sub, carry);
                EXPECT(cpu.Step(memory).stop == CpuStop::Running);
                EXPECT(x[test.dest] == outcome.value);
                EXPECT(cpu.context.nzcv ==
                       (test.flags ? outcome.flags : static_cast<std::uint8_t>(8 | (carry ? 2 : 0))));
            }
        }
    }
    cpu.context.pc = 0x1028;
    x[0] = 0x123456789abcdef0ULL;
    cpu.context.nzcv = 0x6;
    EXPECT(cpu.Step(memory).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x1028 && x[0] == 0x123456789abcdef0ULL);
    EXPECT(cpu.context.nzcv == 0x6);
    std::cout << "A64 ADC/ADCS/SBC/SBCS and 1024 carry-chain oracle vectors passed\n";
    return EXIT_SUCCESS;
}
