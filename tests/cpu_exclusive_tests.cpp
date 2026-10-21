#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "exclusive atomic failure: " << #expr << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

// All 22 words independently assembled from original_atom.s.
constexpr std::array<std::uint32_t, 22> kExclusiveWords = {
    0x085f7c20, 0x085ffc62, 0x08047cc5, 0x0807fd28,
    0x485f7d6a, 0x485ffdac, 0x480e7e0f, 0x4811fe72,
    0x885f7eb4, 0x885ffef6, 0x88187f59, 0x881bffbc,
    0xc85f7c20, 0xc85ffc62, 0xc8047cc5, 0xc807fd28,
    0xd5033f5f, 0xd503305f, 0xd5033bbf, 0xd5033f9f,
    0xd5033fdf, 0xd65f03c0
};

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    GuestMemory memory(0x10000);
    CHECK(memory.Map(0x1000, 0x1000, Access::Read | Access::Write));
    CHECK(memory.Map(0x2000, 0x1000, Access::Read | Access::Write));
    CHECK(memory.Map(0x3000, 0x1000, Access::Read | Access::Write));
    CHECK(memory.Alias(0x4000, 0x3000, 0x1000, Access::Read | Access::Write));
    for (std::size_t i = 0; i < kExclusiveWords.size(); ++i)
        CHECK(memory.Write32(0x1000 + 4 * i, kExclusiveWords[i]));
    CHECK(memory.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    const auto run = [&](unsigned i) {
        cpu.context.pc = 0x1000 + 4 * i;
        return cpu.Step(memory);
    };

    CHECK(memory.Write8(0x3000, 0x44));
    cpu.context.x[1] = cpu.context.x[6] = 0x3000;
    cpu.context.x[5] = 0xa9;
    CHECK(run(0).stop == CpuStop::Running);
    CHECK(cpu.context.x[0] == 0x44);
    CHECK(run(2).stop == CpuStop::Running);
    CHECK(cpu.context.x[4] == 0 && memory.Read8(0x3000) == 0xa9);
    CHECK(run(2).stop == CpuStop::Running);
    CHECK(cpu.context.x[4] == 1 && memory.Read8(0x3000) == 0xa9);

    // Aliases address the same physical frame, so a write loses reservation.
    cpu.context.x[3] = cpu.context.x[9] = 0x3000;
    cpu.context.x[8] = 0xee;
    CHECK(run(1).stop == CpuStop::Running);
    CHECK(memory.Write8(0x4000, 0x33));
    CHECK(run(3).stop == CpuStop::Running);
    CHECK(cpu.context.x[7] == 1 && memory.Read8(0x3000) == 0x33);

    CHECK(memory.Write16(0x3002, 0xbeef));
    cpu.context.x[11] = cpu.context.x[16] = 0x3002;
    cpu.context.x[15] = 0xface;
    CHECK(run(4).stop == CpuStop::Running && cpu.context.x[10] == 0xbeef);
    CHECK(run(6).stop == CpuStop::Running);
    CHECK(cpu.context.x[14] == 0 && memory.Read16(0x3002) == 0xface);

    CHECK(memory.Write32(0x3004, 0x10203040));
    cpu.context.x[23] = cpu.context.x[26] = 0x3004;
    cpu.context.x[25] = 0x50607080;
    CHECK(run(9).stop == CpuStop::Running && cpu.context.x[22] == 0x10203040);
    CHECK(run(10).stop == CpuStop::Running);
    CHECK(cpu.context.x[24] == 0 && memory.Read32(0x3004) == 0x50607080);

    CHECK(memory.Write64(0x3008, 0x0123456789abcdefULL));
    cpu.context.x[1] = cpu.context.x[6] = 0x3008;
    cpu.context.x[5] = 0xfedcba9876543210ULL;
    CHECK(run(12).stop == CpuStop::Running && cpu.context.x[0] == 0x0123456789abcdefULL);
    CHECK(run(14).stop == CpuStop::Running);
    CHECK(cpu.context.x[4] == 0 && memory.Read64(0x3008) == 0xfedcba9876543210ULL);
    CHECK(run(12).stop == CpuStop::Running);
    CHECK(run(16).stop == CpuStop::Running);
    CHECK(run(14).stop == CpuStop::Running && cpu.context.x[4] == 1);
    CHECK(run(12).stop == CpuStop::Running);
    CHECK(run(17).stop == CpuStop::Running);
    CHECK(run(14).stop == CpuStop::Running && cpu.context.x[4] == 1);

    CHECK(run(18).stop == CpuStop::Running); // DMB ISH
    CHECK(run(19).stop == CpuStop::Running); // DSB SY
    CHECK(run(20).stop == CpuStop::Running); // ISB
    CHECK(memory.Write32(0x2000, 0xd50330bf)); // reserved DMB option
    CHECK(memory.Protect(0x2000, 0x1000, Access::Read | Access::Execute));
    cpu.context.pc = 0x2000;
    CHECK(cpu.Step(memory).stop == CpuStop::UndefinedInstruction);
    CHECK(cpu.context.pc == 0x2000);

    CpuInterpreter other;
    other.context.x[1] = 0x3008;
    cpu.context.x[1] = cpu.context.x[6] = 0x3008;
    CHECK(run(12).stop == CpuStop::Running);
    other.context.pc = 0x1000 + 4 * 12;
    CHECK(other.Step(memory).stop == CpuStop::Running);
    CHECK(memory.Write64(0x4008, 0xaabbccdd)); // physical alias invalidates both
    CHECK(run(14).stop == CpuStop::Running && cpu.context.x[4] == 1);

    cpu.context.x[1] = 0x3001;
    CHECK(run(12).stop == CpuStop::DataFault);
    CHECK(cpu.context.pc == 0x1000 + 4 * 12);
    CHECK(memory.Protect(0x3000, 0x1000, Access::Read));
    CHECK(memory.Protect(0x4000, 0x1000, Access::Read));
    cpu.context.x[1] = cpu.context.x[6] = 0x3008;
    CHECK(run(12).stop == CpuStop::Running);
    CHECK(run(14).stop == CpuStop::DataFault);
    CHECK(cpu.context.pc == 0x1000 + 4 * 14);
    CHECK(memory.Read64(0x3008) == 0xaabbccddULL);

    std::cout << "Exclusive atomic widths, alias monitors, CLREX,"
                 " barriers and fault rollback passed\n";
    return EXIT_SUCCESS;
}
