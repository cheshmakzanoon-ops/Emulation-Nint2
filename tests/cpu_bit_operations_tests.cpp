#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    // Independently assembled with Clang AArch64; original source in
    // tests/fixtures/original_bit_ops.s. The final two encodings are invalid.
    constexpr std::array<std::uint32_t, 14> code = {
        0x5ac00020, 0xdac00062, 0x5ac004a4, 0xdac004e6,
        0xdac00928, 0x5ac0096a, 0xdac00dac, 0x5ac011ee,
        0xdac01230, 0x5ac01672, 0xdac016b4, 0xd4200000,
        0x5ac00dac, 0x5ac10020
    };
    GuestMemory mem(0x10000);
    EXPECT(mem.Map(0x1000, 0x1000, Access::Read | Access::Write));
    for (std::size_t i = 0; i < code.size(); ++i)
        EXPECT(mem.Write32(0x1000 + 4 * i, code[i]));
    EXPECT(mem.Protect(0x1000, 0x1000, Access::Read | Access::Execute));

    CpuInterpreter cpu;
    cpu.context.x[1] = 0x01234567;
    cpu.context.x[3] = 0x0123456789abcdefULL;
    cpu.context.x[5] = 0x12345678;
    cpu.context.x[7] = 0x0102030405060708ULL;
    cpu.context.x[9] = 0x0123456789abcdefULL;
    cpu.context.x[11] = 0xdeadbeef;
    cpu.context.x[13] = 0x0123456789abcdefULL;
    cpu.context.x[15] = 0x00100000;
    cpu.context.x[17] = 0x40;
    cpu.context.x[19] = 0xff000000;
    cpu.context.x[21] = 0xfffffffffffffff0ULL;
    cpu.context.nzcv = 0x7;
    for (unsigned i = 0; i < 11; ++i) {
        cpu.context.pc = 0x1000 + 4 * i;
        EXPECT(cpu.Step(mem).stop == CpuStop::Running);
    }
    constexpr auto reverse_bits = [](std::uint64_t value, unsigned width) {
        std::uint64_t result = 0;
        for (unsigned i = 0; i < width; ++i)
            result = (result << 1) | ((value >> i) & 1ULL);
        return result;
    };
    EXPECT(cpu.context.x[0] == reverse_bits(0x01234567, 32));
    EXPECT(cpu.context.x[2] == reverse_bits(0x0123456789abcdefULL, 64));
    EXPECT(cpu.context.x[4] == 0x34127856);
    EXPECT(cpu.context.x[6] == 0x0201040306050807ULL);
    EXPECT(cpu.context.x[8] == 0x67452301efcdab89ULL);
    EXPECT(cpu.context.x[10] == 0xefbeaddeULL);
    EXPECT(cpu.context.x[12] == 0xefcdab8967452301ULL);
    EXPECT(cpu.context.x[14] == 11);
    EXPECT(cpu.context.x[16] == 57);
    EXPECT(cpu.context.x[18] == 7);
    EXPECT(cpu.context.x[20] == 59);
    EXPECT(cpu.context.nzcv == 0x7);

    // Independently calculate 32/64-bit CLZ for deterministic random vectors.
    // The W view must ignore the upper 32 bits of the source register.
    std::uint64_t seed = 0xabcdeffedcba0123ULL;
    for (unsigned i = 0; i < 1024; ++i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        cpu.context.x[15] = seed;
        cpu.context.pc = 0x101c; // CLZ W14,W15
        EXPECT(cpu.Step(mem).stop == CpuStop::Running);
        EXPECT(cpu.context.x[14] == static_cast<unsigned>(
            std::countl_zero(static_cast<std::uint32_t>(seed))));
        cpu.context.x[17] = seed;
        cpu.context.pc = 0x1020; // CLZ X16,X17
        EXPECT(cpu.Step(mem).stop == CpuStop::Running);
        EXPECT(cpu.context.x[16] == static_cast<unsigned>(std::countl_zero(seed)));
    }
    cpu.context.pc = 0x1030; // unallocated 32-bit REV (64-bit group)
    EXPECT(cpu.Step(mem).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x1030);
    cpu.context.pc = 0x1034; // reserved [20:16] bits
    EXPECT(cpu.Step(mem).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x1034);
    EXPECT(cpu.context.nzcv == 0x7);
    std::cout << "AArch64 RBIT/REV/CLZ/CLS and 1024 vector checks passed\n";
    return EXIT_SUCCESS;
}
