#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " at line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    // Original Clang AArch64 assembly: tests/fixtures/original_signed_loads.s
    constexpr std::array<std::uint32_t, 16> code = {
        0x39c00020, 0x39800422, 0x79c00423, 0x79800824,
        0xb9800825, 0x38dff026, 0x789fe027, 0xb89fc028,
        0x38ea6829, 0x78ac582b, 0xb8ae782d, 0x38c0142f,
        0x789fec30, 0xd4200000, 0x3d000020, 0xb9c00825
    };
    GuestMemory mem(0x8000);
    EXPECT(mem.Map(0x1000, 0x1000, Access::Read | Access::Write));
    EXPECT(mem.Map(0x3000, 0x1000, Access::Read | Access::Write));
    for (std::size_t i = 0; i < code.size(); ++i)
        EXPECT(mem.Write32(0x1000 + i * 4, code[i]));
    EXPECT(mem.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
    constexpr std::uint64_t base = 0x3080;
    EXPECT(mem.Write32(base - 4, 0x80345678U));
    EXPECT(mem.Write8(base, 0x80));
    EXPECT(mem.Write8(base + 1, 0xfe));
    EXPECT(mem.Write16(base + 2, 0x8001));
    EXPECT(mem.Write16(base + 4, 0xfffe));
    EXPECT(mem.Write32(base + 8, 0x80010203U));

    CpuInterpreter cpu;
    cpu.context.x[1] = base;
    cpu.context.x[10] = 1;
    cpu.context.x[12] = 2;
    cpu.context.x[14] = 2;
    cpu.context.nzcv = 0xa;
    for (unsigned i = 0; i < 13; ++i) {
        cpu.context.pc = 0x1000 + i * 4;
        EXPECT(cpu.Step(mem).stop == CpuStop::Running);
    }
    EXPECT(cpu.context.pc == 0x1034);
    EXPECT(cpu.context.x[0] == 0xffffff80ULL);            // LDRSB W
    EXPECT(cpu.context.x[2] == UINT64_MAX - 1);            // LDRSB X
    EXPECT(cpu.context.x[3] == 0xffff8001ULL);             // LDRSH W
    EXPECT(cpu.context.x[4] == UINT64_MAX - 1);            // LDRSH X
    EXPECT(cpu.context.x[5] == 0xffffffff80010203ULL);     // LDRSW X
    EXPECT(cpu.context.x[6] == 0xffffff80ULL);             // LDURSB W
    EXPECT(cpu.context.x[7] == 0xffffffffffff8034ULL);     // LDURSH X
    EXPECT(cpu.context.x[8] == 0xffffffff80345678ULL);     // LDURSW X
    EXPECT(cpu.context.x[9] == 0xfffffffeULL);             // register offset
    EXPECT(cpu.context.x[11] == UINT64_MAX - 1);           // UXTW scaled
    EXPECT(cpu.context.x[13] == 0xffffffff80010203ULL);    // LSL scaled
    EXPECT(cpu.context.x[15] == 0xffffff80ULL);            // post-index
    EXPECT(cpu.context.x[16] == 0xffffffffffff8080ULL);    // pre-index
    EXPECT(cpu.context.x[1] == base - 1);                  // post then pre
    EXPECT(cpu.context.nzcv == 0xa);                      // loads do not set flags

    // Destination register, base writeback and PC must survive memory faults.
    cpu.context.x[1] = 0x3ffd;
    cpu.context.x[4] = 0x12345678;
    cpu.context.pc = 0x100c; // LDRSH X4,[X1,#4] -> unmapped 0x4001
    auto result = cpu.Step(mem);
    EXPECT(result.stop == CpuStop::DataFault && result.fault_address == 0x4001);
    EXPECT(cpu.context.pc == 0x100c && cpu.context.x[4] == 0x12345678);
    cpu.context.x[1] = 0x3000;
    cpu.context.x[16] = 0x9988;
    cpu.context.pc = 0x1030; // pre-index -> unmapped 0x2ffe
    result = cpu.Step(mem);
    EXPECT(result.stop == CpuStop::DataFault && result.fault_address == 0x2ffe);
    EXPECT(cpu.context.pc == 0x1030 && cpu.context.x[1] == 0x3000);
    EXPECT(cpu.context.x[16] == 0x9988);
    cpu.context.x[1] = 0x4000;
    cpu.context.x[15] = 0x777;
    cpu.context.pc = 0x102c; // post-index reading unmapped page
    result = cpu.Step(mem);
    EXPECT(result.stop == CpuStop::DataFault && result.fault_address == 0x4000);
    EXPECT(cpu.context.pc == 0x102c && cpu.context.x[1] == 0x4000);
    EXPECT(cpu.context.x[15] == 0x777);

    cpu.context.pc = 0x1038; // SIMD STR B must trap; never fake scalar memory write
    EXPECT(cpu.Step(mem).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x1038);
    cpu.context.pc = 0x103c; // reserved size/opc combination (LDRSW-to-W)
    EXPECT(cpu.Step(mem).stop == CpuStop::UndefinedInstruction);
    EXPECT(cpu.context.pc == 0x103c);
    EXPECT(cpu.context.nzcv == 0xa);
    std::cout << "AArch64 signed scaled/unscaled/register loads, faults and SIMD traps passed\n";
    return EXIT_SUCCESS;
}
