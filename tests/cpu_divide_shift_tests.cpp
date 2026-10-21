#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "nint2/cpu.hpp"

#define EXPECT(check) do { if (!(check)) { \
    std::cerr << "FAILED: " #check << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
std::uint64_t SignedQuotient(std::uint64_t a, std::uint64_t b, bool wide) {
    if (wide) {
        const auto x = std::bit_cast<std::int64_t>(a);
        const auto y = std::bit_cast<std::int64_t>(b);
        if (y == 0) return 0;
        if (x == std::numeric_limits<std::int64_t>::min() && y == -1) return a;
        return static_cast<std::uint64_t>(x / y);
    }
    const auto x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a));
    const auto y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(b));
    if (y == 0) return 0;
    if (x == std::numeric_limits<std::int32_t>::min() && y == -1)
        return static_cast<std::uint32_t>(a);
    return static_cast<std::uint32_t>(x / y);
}
} // namespace

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    // Encodings from tests/fixtures/original_divide_shift.s (Clang AArch64).
    constexpr std::array<std::uint32_t, 12> instructions{
        0x1ac20820, 0x1ac50c83, 0x9ac808e6, 0x9acb0d49,
        0x1ace21ac, 0x9ad1260f, 0x1ad42a72, 0x9ad72ad5,
        0x1ada2f38, 0x9add2f9b, 0x1ac03020, 0xd4200000
    };
    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 4096, Access::Read | Access::Write));
    for (std::size_t i = 0; i < instructions.size(); ++i)
        EXPECT(memory.Write32(0x1000 + i * 4, instructions[i]));
    EXPECT(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    auto& x = cpu.context.x;
    x[1] = 100; x[2] = 7;
    x[4] = static_cast<std::uint32_t>(-100); x[5] = 7;
    x[7] = UINT64_MAX; x[8] = 10;
    x[10] = std::uint64_t{1} << 63; x[11] = UINT64_MAX;
    x[13] = 0x80000001; x[14] = 33;
    x[16] = std::uint64_t{1} << 63; x[17] = 65;
    x[19] = 0x80000000; x[20] = 36;
    x[22] = std::uint64_t{1} << 63; x[23] = 67;
    x[25] = 0x80000001; x[26] = 33;
    x[28] = (std::uint64_t{1} << 63) | 1; x[29] = 65;
    cpu.context.nzcv = 0x9;
    for (unsigned i = 0; i < 10; ++i) {
        cpu.context.pc = 0x1000 + 4 * i;
        EXPECT(cpu.Step(memory).stop == CpuStop::Running);
        EXPECT(cpu.context.nzcv == 0x9);
        EXPECT(cpu.context.pc == 0x1000 + 4 * (i + 1));
    }
    EXPECT(x[0] == 14);
    EXPECT(x[3] == 0xfffffff2ULL);
    EXPECT(x[6] == UINT64_MAX / 10);
    EXPECT(x[9] == (std::uint64_t{1} << 63));
    EXPECT(x[12] == 3);
    EXPECT(x[15] == (std::uint64_t{1} << 62));
    EXPECT(x[18] == 0xf8000000ULL);
    EXPECT(x[21] == 0xf000000000000000ULL);
    EXPECT(x[24] == 0xc0000000ULL);
    EXPECT(x[27] == 0xc000000000000000ULL);

    // Zero divisors and signed overflow have ARM-defined (not C++ UB) results.
    x[1] = 0; x[2] = 0; x[4] = 123; x[5] = 0; x[7] = 0; x[8] = 0;
    x[10] = std::uint64_t{1} << 63; x[11] = 0;
    for (unsigned i = 0; i < 4; ++i) {
        cpu.context.pc = 0x1000 + 4 * i;
        EXPECT(cpu.Step(memory).stop == CpuStop::Running);
    }
    EXPECT(x[0] == 0 && x[3] == 0 && x[6] == 0 && x[9] == 0);
    x[4] = 0x80000000U; x[5] = UINT32_MAX;
    cpu.context.pc = 0x1004;
    EXPECT(cpu.Step(memory).stop == CpuStop::Running && x[3] == 0x80000000U);

    // Independent C++20 scalar oracles, including high shift-count bits and
    // zero-extended W-register results, on deterministic generated values.
    std::uint64_t seed = 0xdecafbad5ea551edULL;
    for (unsigned i = 0; i < 1024; ++i) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto a = seed;
        seed = seed * 2862933555777941757ULL + 3037000493ULL;
        const auto b = seed;
        x[1] = a; x[2] = b; x[4] = a; x[5] = b;
        x[7] = a; x[8] = b; x[10] = a; x[11] = b;
        x[13] = a; x[14] = b; x[16] = a; x[17] = b;
        x[19] = a; x[20] = b; x[22] = a; x[23] = b;
        x[25] = a; x[26] = b; x[28] = a; x[29] = b;
        for (unsigned j = 0; j < 10; ++j) {
            cpu.context.pc = 0x1000 + 4 * j;
            EXPECT(cpu.Step(memory).stop == CpuStop::Running);
        }
        const auto aw = static_cast<std::uint32_t>(a);
        const auto bw = static_cast<std::uint32_t>(b);
        EXPECT(x[0] == (bw == 0 ? 0 : aw / bw));
        EXPECT(x[3] == SignedQuotient(a, b, false));
        EXPECT(x[6] == (b == 0 ? 0 : a / b));
        EXPECT(x[9] == SignedQuotient(a, b, true));
        EXPECT(x[12] == static_cast<std::uint32_t>(aw << (bw & 31)));
        EXPECT(x[15] == (a >> (b & 63)));
        EXPECT(x[18] == static_cast<std::uint32_t>(
            std::bit_cast<std::int32_t>(aw) >> (bw & 31)));
        EXPECT(x[21] == static_cast<std::uint64_t>(
            std::bit_cast<std::int64_t>(a) >> (b & 63)));
        EXPECT(x[24] == std::rotr(aw, static_cast<int>(bw & 31)));
        EXPECT(x[27] == std::rotr(a, static_cast<int>(b & 63)));
        EXPECT(cpu.context.nzcv == 0x9);
    }

    cpu.context.pc = 0x1028; // reserved operation within data-processing (2-source)
    x[0] = 0xfeedface;
    EXPECT(cpu.Step(memory).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x1028 && x[0] == 0xfeedface);
    EXPECT(cpu.context.nzcv == 0x9);
    std::cout << "A64 UDIV/SDIV/LSLV/LSRV/ASRV/RORV and 1024 vector sets passed\n";
    return EXIT_SUCCESS;
}
