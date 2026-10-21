#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "AArch64 EL0 register test failure: " << #expr \
              << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

// Prefix contains 12 independently assembled original_sysregs.s instructions.
// Last four are synthetic, source-authored corner-case encodings.
constexpr std::array<std::uint32_t, 16> kSystemWords{
    0xd53b4200, 0xd51b4201, 0xd53bd042, 0xd51bd043,
    0xd53bd064, 0xd53b4405, 0xd51b4406, 0xd53b4427,
    0xd51b4428, 0xd53be009, 0xd53be04a, 0xd65f03c0,
    0xd51b421f, 0xd53b421f, 0xd51bd05f, 0x54000040
};

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::GuestMemory;
    GuestMemory mem(0x5000);
    CHECK(mem.Map(0x1000, 0x1000, Access::Read | Access::Write));
    for (unsigned i = 0; i < kSystemWords.size(); ++i)
        CHECK(mem.Write32(0x1000 + 4 * i, kSystemWords[i]));
    CHECK(mem.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
    CpuInterpreter cpu;
    const auto run = [&](unsigned i) {
        cpu.context.pc = 0x1000 + 4 * i;
        return cpu.Step(mem);
    };

    // NZCV architectural result occupies bits 31:28; all others are zero.
    cpu.context.nzcv = 0xb;
    cpu.context.x[0] = UINT64_MAX;
    CHECK(run(0).stop == CpuStop::Running);
    CHECK(cpu.context.x[0] == 0xb0000000ULL);
    CHECK(cpu.context.pc == 0x1004);
    cpu.context.x[1] = UINT64_C(0xfffb0000a5a5a5a5);
    CHECK(run(1).stop == CpuStop::Running);
    CHECK(cpu.context.nzcv == 0);
    cpu.context.x[1] = UINT64_C(0xfff0000000000000);
    CHECK(run(1).stop == CpuStop::Running && cpu.context.nzcv == 0);

    // All sixteen flag combinations, with arbitrary unrelated register bits.
    std::uint32_t random = 0xb6f947cc;
    for (unsigned n = 0; n < 512; ++n) {
        random = random * 1664525U + 1013904223U;
        const auto flags = n & 15U;
        const auto input = ((std::uint64_t{random} << 32) | random) &
                           ~UINT64_C(0xf0000000);
        cpu.context.x[1] = input | (std::uint64_t{flags} << 28);
        CHECK(run(1).stop == CpuStop::Running);
        CHECK(cpu.context.nzcv == flags);
        CHECK(run(0).stop == CpuStop::Running);
        CHECK(cpu.context.x[0] == (std::uint64_t{flags} << 28));
    }

    // A64 conditional branch consumes the flags written by MSR NZCV.
    CHECK(run(15).stop == CpuStop::Running);
    CHECK(cpu.context.pc == 0x1000 + 4 * 15 + 4); // n=511 -> NZCV = 0xf, Z set
    // Above B.EQ branch uses PC+8 when Z is set, not PC+4.
    cpu.context.nzcv = 4;
    CHECK(run(15).stop == CpuStop::Running);
    CHECK(cpu.context.pc == 0x1000 + 4 * 15 + 8);
    cpu.context.nzcv = 0;
    CHECK(run(15).stop == CpuStop::Running);
    CHECK(cpu.context.pc == 0x1000 + 4 * 15 + 4);

    // Guest-chosen TPIDR_EL0 roundtrips a full 64-bit value, including zero.
    cpu.context.x[3] = UINT64_C(0xf123456789abcdef);
    CHECK(run(3).stop == CpuStop::Running);
    CHECK(cpu.context.tpidr_el0 == cpu.context.x[3]);
    CHECK(run(2).stop == CpuStop::Running);
    CHECK(cpu.context.x[2] == cpu.context.x[3]);
    cpu.context.x[3] = 0;
    CHECK(run(3).stop == CpuStop::Running);
    CHECK(run(2).stop == CpuStop::Running && cpu.context.x[2] == 0);

    // Zero register is permitted for system register source and destination.
    cpu.context.nzcv = 15;
    CHECK(run(12).stop == CpuStop::Running && cpu.context.nzcv == 0);
    cpu.context.nzcv = 15;
    CHECK(run(13).stop == CpuStop::Running && cpu.context.nzcv == 15);
    cpu.context.tpidr_el0 = 0x123;
    CHECK(run(14).stop == CpuStop::Running && cpu.context.tpidr_el0 == 0);

    // The thread pointer belongs to one interpreter context, not the host or
    // another synthetic process sharing an executable page.
    CpuInterpreter other;
    other.context.pc = 0x1000 + 4 * 2;
    CHECK(other.Step(mem).stop == CpuStop::Running);
    CHECK(other.context.x[2] == 0);
    other.context.x[3] = 0x987654321;
    other.context.pc = 0x1000 + 4 * 3;
    CHECK(other.Step(mem).stop == CpuStop::Running);
    CHECK(other.context.tpidr_el0 == 0x987654321ULL);
    CHECK(cpu.context.tpidr_el0 == 0);

    // Deliberately unknown architectural/hardware state remains unsupported.
    // No fake FP, clock or read-only TLS values are returned.
    for (unsigned i : {4U, 5U, 6U, 7U, 8U, 9U, 10U}) {
        const auto x = cpu.context.x;
        const auto flags = cpu.context.nzcv;
        const auto tls = cpu.context.tpidr_el0;
        const auto result = run(i);
        CHECK(result.stop == CpuStop::UndefinedInstruction);
        CHECK(result.instruction == kSystemWords[i]);
        CHECK(cpu.context.pc == 0x1000 + 4 * i);
        CHECK(cpu.context.x == x);
        CHECK(cpu.context.nzcv == flags);
        CHECK(cpu.context.tpidr_el0 == tls);
    }

    std::cout << "EL0 NZCV/TPIDR_EL0 MRS/MSR, 512 NZCV vectors,"
                 " context isolation and unsupported-register traps passed\n";
    return EXIT_SUCCESS;
}
