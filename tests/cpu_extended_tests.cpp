#include <array>
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
    // Independently assembled with Clang AArch64. Original mnemonic source
    // is tests/fixtures/original_integer_extensions.s.
    constexpr std::array<std::uint32_t, 23> code = {
        0xa9bf0be1, 0xa8c113e3, 0xf81f80c5, 0xb85fc0c7,
        0x382a6928, 0x386a692b, 0x361801ec, 0xb74001cd,
        0x580001ce, 0x980001af, 0x9a920230, 0x1a951693,
        0x5a9802f6, 0xda9b1759, 0x9b020c20, 0x1b069ca4,
        0x93407d28, 0xd3485d6a, 0x13042dac, 0xd37c1dee,
        0x937c1e30, 0xd65f03c0, 0xffffffff
    };
    GuestMemory mem(0x8000);
    EXPECT(mem.Map(0x1000, 0x1000, Access::Read | Access::Write));
    EXPECT(mem.Map(0x3000, 0x1000, Access::Read | Access::Write));
    for (std::size_t i = 0; i < code.size(); ++i)
        EXPECT(mem.Write32(0x1000 + 4 * i, code[i]));
    EXPECT(mem.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    auto run = [&](unsigned index) {
        cpu.context.pc = 0x1000 + 4 * index;
        return cpu.Step(mem);
    };

    cpu.context.sp = 0x3010;
    cpu.context.x[1] = 0x1122334455667788ULL;
    cpu.context.x[2] = 0x8877665544332211ULL;
    EXPECT(run(0).stop == CpuStop::Running);
    EXPECT(cpu.context.sp == 0x3000);
    EXPECT(mem.Read64(0x3000) == cpu.context.x[1]);
    EXPECT(mem.Read64(0x3008) == cpu.context.x[2]);
    EXPECT(run(1).stop == CpuStop::Running);
    EXPECT(cpu.context.sp == 0x3010);
    EXPECT(cpu.context.x[3] == cpu.context.x[1] && cpu.context.x[4] == cpu.context.x[2]);

    cpu.context.x[6] = 0x3020;
    cpu.context.x[5] = 0x1234567887654321ULL;
    EXPECT(run(2).stop == CpuStop::Running);
    EXPECT(mem.Read64(0x3018) == cpu.context.x[5]);
    EXPECT(run(3).stop == CpuStop::Running);
    EXPECT(cpu.context.x[7] == 0x12345678ULL);
    cpu.context.x[9] = 0x3000;
    cpu.context.x[10] = 7;
    cpu.context.x[8] = 0xab;
    EXPECT(run(4).stop == CpuStop::Running);
    EXPECT(mem.Read8(0x3007) == 0xab);
    EXPECT(run(5).stop == CpuStop::Running);
    EXPECT(cpu.context.x[11] == 0xab);

    cpu.context.x[12] = 0;
    EXPECT(run(6).stop == CpuStop::Running && cpu.context.pc == 0x1054);
    cpu.context.x[12] = 8;
    EXPECT(run(6).stop == CpuStop::Running && cpu.context.pc == 0x101c);
    cpu.context.x[13] = 1ULL << 40;
    EXPECT(run(7).stop == CpuStop::Running && cpu.context.pc == 0x1054);
    cpu.context.x[13] = 0;
    EXPECT(run(7).stop == CpuStop::Running && cpu.context.pc == 0x1020);
    EXPECT(run(8).stop == CpuStop::Running && cpu.context.x[14] == 0xffffffffULL);
    EXPECT(run(9).stop == CpuStop::Running && cpu.context.x[15] == UINT64_MAX);

    cpu.context.nzcv = 4; // Z=1, EQ true, NE false
    cpu.context.x[17] = 123;
    cpu.context.x[18] = 456;
    EXPECT(run(10).stop == CpuStop::Running && cpu.context.x[16] == 123);
    cpu.context.x[20] = 33;
    cpu.context.x[21] = 41;
    EXPECT(run(11).stop == CpuStop::Running && cpu.context.x[19] == 42);
    cpu.context.x[23] = 29;
    cpu.context.x[24] = 36;
    EXPECT(run(12).stop == CpuStop::Running && cpu.context.x[22] == 29);
    cpu.context.x[26] = 9;
    cpu.context.x[27] = 10;
    EXPECT(run(13).stop == CpuStop::Running &&
           cpu.context.x[25] == static_cast<std::uint64_t>(-10LL));
    cpu.context.x[1] = 7;
    cpu.context.x[2] = 6;
    cpu.context.x[3] = 1;
    EXPECT(run(14).stop == CpuStop::Running && cpu.context.x[0] == 43);
    cpu.context.x[5] = 2;
    cpu.context.x[6] = 3;
    cpu.context.x[7] = 20;
    EXPECT(run(15).stop == CpuStop::Running && cpu.context.x[4] == 14);

    cpu.context.x[9] = 0xffffffffULL;
    EXPECT(run(16).stop == CpuStop::Running && cpu.context.x[8] == UINT64_MAX);
    cpu.context.x[11] = 0x00abcdef00ULL;
    EXPECT(run(17).stop == CpuStop::Running && cpu.context.x[10] == 0xcdef);
    cpu.context.x[13] = 0xf00;
    EXPECT(run(18).stop == CpuStop::Running && cpu.context.x[12] == 0xfffffff0ULL);
    cpu.context.x[15] = 0xaa;
    EXPECT(run(19).stop == CpuStop::Running && cpu.context.x[14] == 0xaa0);
    cpu.context.x[17] = 0x80;
    EXPECT(run(20).stop == CpuStop::Running &&
           cpu.context.x[16] == UINT64_C(0xfffffffffffff800));

    // A pair spanning into an unmapped page cannot partially write or update SP.
    cpu.context.sp = 0x4008;
    cpu.context.x[1] = 9;
    cpu.context.x[2] = 10;
    EXPECT(run(0).stop == CpuStop::DataFault && cpu.context.pc == 0x1000);
    EXPECT(cpu.context.sp == 0x4008 && mem.Read64(0x3ff8) == 0);

    // An LDP fault on the SECOND word must preserve both registers and SP.
    cpu.context.sp = 0x3ff8;
    cpu.context.x[3] = 111;
    cpu.context.x[4] = 222;
    EXPECT(run(1).stop == CpuStop::DataFault && cpu.context.pc == 0x1004);
    EXPECT(cpu.context.sp == 0x3ff8);
    EXPECT(cpu.context.x[3] == 111 && cpu.context.x[4] == 222);

    std::cout << "AArch64 Clang-assembled extended integer and fault tests passed\n";
    return EXIT_SUCCESS;
}
