#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define EXPECT(check) do { if (!(check)) { \
    std::cerr << "FAILED: " #check << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;

    // Independent Clang AArch64 assembly in original_extract.s.
    constexpr std::array<std::uint32_t, 13> words{
        0x13820020, 0x13853483, 0x13887ce6, 0x93cb0149,
        0x93ce1dac, 0x93d1fe0f, 0x13931672, 0x93d546b4,
        0xd65f03c0, 0x13c20020, 0x13828020, 0x93820020,
        0x13a20020
    };
    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 4096, Access::Read | Access::Write));
    for (std::size_t i = 0; i < words.size(); ++i)
        EXPECT(memory.Write32(0x1000 + 4 * i, words[i]));
    EXPECT(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    auto& registers = cpu.context.x;
    struct Case { unsigned instruction, dst, rn, rm, shift; bool wide; };
    constexpr std::array<Case, 8> cases{{
        {0, 0, 1, 2, 0, false},
        {1, 3, 4, 5, 13, false},
        {2, 6, 7, 8, 31, false},
        {3, 9, 10, 11, 0, true},
        {4, 12, 13, 14, 7, true},
        {5, 15, 16, 17, 63, true},
        {6, 18, 19, 19, 5, false},
        {7, 20, 21, 21, 17, true}
    }};
    std::uint64_t seed = 0x9e3779b97f4a7c15ULL;
    for (unsigned iteration = 0; iteration < 2048; ++iteration) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto source_a = seed;
        seed = seed * 2862933555777941757ULL + 3037000493ULL;
        const auto source_b = seed;
        for (const auto& test : cases) {
            registers[test.rn] = source_a;
            registers[test.rm] = source_b;
            const auto high = test.wide ? registers[test.rn]
                                        : static_cast<std::uint32_t>(registers[test.rn]);
            const auto low = test.wide ? registers[test.rm]
                                       : static_cast<std::uint32_t>(registers[test.rm]);
            const auto mask = test.wide ? UINT64_MAX : std::uint64_t{UINT32_MAX};
            const unsigned width = test.wide ? 64 : 32;
            const auto expected = (test.shift == 0 ? low :
                ((low >> test.shift) | (high << (width - test.shift)))) & mask;
            cpu.context.pc = 0x1000 + 4 * test.instruction;
            cpu.context.nzcv = 0xb;
            EXPECT(cpu.Step(memory).stop == CpuStop::Running);
            EXPECT(cpu.context.pc == 0x1000 + 4 * (test.instruction + 1));
            EXPECT(registers[test.dst] == expected);
            EXPECT(cpu.context.nzcv == 0xb);
            if (test.rn == test.rm) {
                if (test.wide)
                    EXPECT(registers[test.dst] ==
                           std::rotr(source_b, static_cast<int>(test.shift)));
                else
                    EXPECT(registers[test.dst] ==
                           std::rotr(static_cast<std::uint32_t>(source_b),
                                     static_cast<int>(test.shift)));
            }
        }
    }
    // Illegal N/sf combinations, 32-bit lsb==32, and reserved fixed bit:
    // each must trap without changing destination, PC or condition flags.
    for (unsigned i = 9; i < words.size(); ++i) {
        cpu.context.pc = 0x1000 + 4 * i;
        registers[0] = 0xdeadbeefdeadbeefULL;
        cpu.context.nzcv = 5;
        EXPECT(cpu.Step(memory).stop == CpuStop::UndefinedInstruction);
        EXPECT(cpu.context.pc == 0x1000 + 4 * i);
        EXPECT(registers[0] == 0xdeadbeefdeadbeefULL);
        EXPECT(cpu.context.nzcv == 5);
    }
    std::cout << "A64 EXTR/ROR immediate: 16384 independent vector cases passed\n";
    return EXIT_SUCCESS;
}
