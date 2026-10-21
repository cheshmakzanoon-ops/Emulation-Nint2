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
    constexpr std::uint64_t base = 0x1000;
    GuestMemory memory(0x10000);
    EXPECT(memory.Map(base, 4096, Access::Read | Access::Write));
    EXPECT(memory.Write32(base, 0x528000e0)); // movz w0, #7
    EXPECT(memory.Write32(base + 4, 0x11008c00)); // add w0, w0, #35
    EXPECT(memory.Write32(base + 8, 0xd4200000)); // brk #0
    EXPECT(memory.Protect(base, 4096, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    cpu.context.pc = base;
    const auto run = cpu.Run(memory, 10);
    EXPECT(run.stop == CpuStop::Breakpoint && run.steps == 3);
    EXPECT(cpu.context.x[0] == 42 && cpu.context.pc == base + 8);
    EXPECT(cpu.Step(memory).stop == CpuStop::Breakpoint);

    // All encodings are from our own Clang --target=aarch64-none-elf assembler fixture.
    constexpr std::array<std::uint32_t, 15> program = {
        0xd2824681, // movz x1, 0x1234
        0xf2b579a1, // movk x1, 0xabcd, lsl 16
        0x91000422, // add x2,x1,1
        0xeb020043, // subs x3,x2,x2 (Z=1)
        0x54000040, // b.eq next+8
        0xd2800c60, // movz x0,99 (skipped)
        0xaa0103e4, // orr x4,xzr,x1
        0xca010085, // eor x5,x4,x1
        0xb4000045, // cbz x5,+8
        0xd2800c60, // movz x0,99 (skipped)
        0xf90007e2, // str x2,[sp,#8]
        0xf94007e6, // ldr x6,[sp,#8]
        0x94000002, // bl +8
        0xd4200000, // brk
        0xd65f03c0  // ret
    };
    GuestMemory mem(0x10000);
    EXPECT(mem.Map(0x2000,4096,Access::Read | Access::Write));
    EXPECT(mem.Map(0x3000,4096,Access::Read | Access::Write));
    for (std::size_t i = 0; i < program.size(); ++i)
        EXPECT(mem.Write32(0x2000+4*i,program[i]));
    EXPECT(mem.Protect(0x2000,4096,Access::Read | Access::Execute));
    CpuInterpreter nested;
    nested.context.pc = 0x2000;
    nested.context.sp = 0x3100;
    const auto nested_result = nested.Run(mem, 64);
    EXPECT(nested_result.stop == CpuStop::Breakpoint);
    EXPECT(nested.context.x[0] == 0);
    EXPECT(nested.context.x[1] == 0xabcd1234ULL);
    EXPECT(nested.context.x[2] == 0xabcd1235ULL);
    EXPECT(nested.context.x[3] == 0 && (nested.context.nzcv & 4) != 0);
    EXPECT(nested.context.x[4] == nested.context.x[1]);
    EXPECT(nested.context.x[5] == 0);
    EXPECT(nested.context.x[6] == nested.context.x[2]);
    EXPECT(mem.Read64(0x3108) == nested.context.x[2]);
    EXPECT(nested.context.x[30] == 0x2034);

    // Execute permission and invalid instruction traps do not advance PC.
    CpuInterpreter no_exec;
    no_exec.context.pc = 0x3000;
    EXPECT(no_exec.Run(mem, 1).stop == CpuStop::InstructionFault);
    EXPECT(no_exec.context.pc == 0x3000);
    EXPECT(nested.Run(mem, 0).stop == CpuStop::StepLimit);
    EXPECT(mem.Protect(0x3000,4096,Access::Read | Access::Execute));
    CpuInterpreter invalid;
    invalid.context.pc = 0x3000; // zero-filled instruction
    EXPECT(invalid.Step(mem).stop == CpuStop::UndefinedInstruction);
    EXPECT(invalid.context.pc == 0x3000);

    // Deterministic randomized ADD immediate 32-bit wrap, flags and 64-bit.
    GuestMemory randmem(0x10000);
    EXPECT(randmem.Map(0x4000,4096,Access::Read | Access::Write));
    EXPECT(randmem.Write32(0x4000,0x11000400)); // add w0,w0,#1
    EXPECT(randmem.Write32(0x4004,0x91000400)); // add x0,x0,#1
    EXPECT(randmem.Protect(0x4000,4096,Access::Read | Access::Execute));
    std::uint64_t seed = 0x123456789abcdef0ULL;
    for (unsigned i = 0; i < 512; ++i) {
        seed = seed * 6364136223846793005ULL + 1;
        CpuInterpreter variant;
        variant.context.x[0] = seed;
        variant.context.pc = 0x4000;
        EXPECT(variant.Step(randmem).stop == CpuStop::Running);
        EXPECT(variant.context.x[0] == static_cast<std::uint32_t>(seed + 1));
        variant.context.x[0] = seed;
        variant.context.pc = 0x4004;
        EXPECT(variant.Step(randmem).stop == CpuStop::Running);
        EXPECT(variant.context.x[0] == seed + 1);
    }
    std::cout << "AArch64 arithmetic/branch/load-store/trap tests passed\n";
    return EXIT_SUCCESS;
}
