#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " #condition << " at line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    // Independently Clang 17 assembled tests/fixtures/original_logical_ops.s.
    constexpr std::array<std::uint32_t, 16> code = {
        0x12001c20, 0x32010462, 0xd2089ca4, 0xf2402ce6,
        0x8a2a0128, 0x2a6d0d8b, 0xcab009ee, 0x6a330251,
        0x12008eb4, 0xb20102f6, 0x72000338, 0xf241037a,
        0xd65f03c0, 0x12400020, 0x1200fc20, 0x9240fc20
    };
    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 0x1000, Access::Read | Access::Write));
    for (std::size_t i = 0; i < code.size(); ++i)
        EXPECT(memory.Write32(0x1000 + 4 * i, code[i]));
    EXPECT(memory.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    auto& x = cpu.context.x;
    x[1] = 0xdeadbeefULL;
    x[3] = 0x7ffffffeULL;
    x[5] = 0x1122334455667788ULL;
    x[7] = 0x12345ULL;
    x[9] = 0x0123456789abcdefULL;
    x[10] = 0x00ff00ff00ff00ffULL;
    x[12] = 0x89abcdefULL;
    x[13] = 0xf0f0f0f0ULL;
    x[15] = 0x1234567890abcdefULL;
    x[16] = 0xf000000000000000ULL;
    x[18] = 0x80000000ULL;
    x[19] = 0;
    x[21] = 0x12345678ULL;
    x[23] = 0x1122334455667788ULL;
    x[25] = 2;
    x[27] = 0x8000000000000001ULL;
    for (unsigned i = 0; i < 12; ++i) {
        cpu.context.pc = 0x1000 + i * 4;
        const auto step = cpu.Step(memory);
        EXPECT(step.stop == CpuStop::Running);
        EXPECT(cpu.context.pc == 0x1000 + (i + 1) * 4);
        if (i == 3) EXPECT(cpu.context.nzcv == 0);  // ANDS nonzero, N=0
        if (i == 7) EXPECT(cpu.context.nzcv == 8);  // BICS W negative
        if (i == 10) EXPECT(cpu.context.nzcv == 4); // ANDS W zero
        if (i == 11) EXPECT(cpu.context.nzcv == 8); // ANDS X negative
    }
    EXPECT(x[0] == 0xefULL);
    EXPECT(x[2] == 0xffffffffULL);
    EXPECT(x[4] == 0xee22cc44aa668888ULL);
    EXPECT(x[6] == 0x345ULL);
    EXPECT(x[8] == 0x010045008900cd00ULL);
    EXPECT(x[11] == 0xe9ebedefULL);
    EXPECT(x[14] == 0x11cba9876f543210ULL);
    EXPECT(x[17] == 0x80000000ULL);
    EXPECT(x[20] == 0x40008ULL);
    EXPECT(x[22] == 0x91223344d5667788ULL);
    EXPECT(x[24] == 0);
    EXPECT(x[26] == 0x8000000000000000ULL);
    // Reserved 32-bit N=1, unallocated 32-bit and 64-bit all-ones masks:
    // none may change the destination, NZCV or PC.
    for (unsigned i = 13; i < 16; ++i) {
        cpu.context.pc = 0x1000 + i * 4;
        cpu.context.x[0] = 0xfeedfaceULL;
        cpu.context.nzcv = 6;
        EXPECT(cpu.Step(memory).stop == CpuStop::UndefinedInstruction);
        EXPECT(cpu.context.pc == 0x1000 + i * 4);
        EXPECT(cpu.context.x[0] == 0xfeedfaceULL);
        EXPECT(cpu.context.nzcv == 6);
    }
    std::cout << "AArch64 logical immediate, invert and reserved encoding tests passed\n";
    return EXIT_SUCCESS;
}
