#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "AArch64 atomic pair failure: " #x " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::Access;
using nint2::CpuInterpreter;
using nint2::CpuStop;
using nint2::GuestMemory;

// Independent original_pair.s encoding, Clang 17 AArch64 assembler.
constexpr std::array<std::uint32_t, 17> kPairWords{
    0x887f0440, 0x887f90a3, 0x88262127, 0x882ab1ab,
    0xc87f0440, 0xc87f90a3, 0xc8262127, 0xc82ab1ab,
    0x08207c82, 0x08667d48, 0x082cfe0e, 0x0874ff16,
    0x48207c82, 0x48667d48, 0x482cfe0e, 0x4874ff16,
    0xd65f03c0
};
struct Fixture {
    GuestMemory memory{0x10000};
    CpuInterpreter cpu{};
    Fixture() {
        CHECK(memory.Map(0x1000, 4096, Access::Read | Access::Write));
        CHECK(memory.Map(0x2000, 4096, Access::Read | Access::Write));
        CHECK(memory.Map(0x3000, 4096, Access::Read | Access::Write));
        CHECK(memory.Alias(0x4000, 0x3000, 4096, Access::Read | Access::Write));
        for (unsigned i = 0; i < kPairWords.size(); ++i)
            CHECK(memory.Write32(0x1000 + 4 * i, kPairWords[i]));
        CHECK(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    }
    [[nodiscard]] nint2::CpuResult Step(unsigned i) {
        cpu.context.pc = 0x1000 + 4 * i;
        return cpu.Step(memory);
    }
    [[nodiscard]] nint2::CpuResult Dynamic(std::uint32_t word) {
        CHECK(memory.Write32(0x2000, word));
        CHECK(memory.Protect(0x2000, 4096, Access::Read | Access::Execute));
        cpu.context.pc = 0x2000;
        return cpu.Step(memory);
    }
};
bool TestExclusiveW() {
    Fixture r;
    CHECK(r.memory.Write64(0x3000, UINT64_C(0x1122334455667788)));
    r.cpu.context.x[2] = r.cpu.context.x[9] = 0x3000;
    r.cpu.context.x[7] = 0xaaaabbbb;
    r.cpu.context.x[8] = 0xccccdddd;
    CHECK(r.Step(0).stop == CpuStop::Running);
    CHECK(r.cpu.context.x[0] == 0x55667788);
    CHECK(r.cpu.context.x[1] == 0x11223344);
    CHECK(r.Step(2).stop == CpuStop::Running);
    CHECK(r.cpu.context.x[6] == 0);
    CHECK(r.memory.Read64(0x3000) == UINT64_C(0xccccddddaaaabbbb));
    CHECK(r.memory.Read64(0x4000) == UINT64_C(0xccccddddaaaabbbb));
    CHECK(r.Step(2).stop == CpuStop::Running && r.cpu.context.x[6] == 1);
    CHECK(r.memory.Read64(0x3000) == UINT64_C(0xccccddddaaaabbbb));

    // Alias writes invalidate reservations for both elements together.
    r.cpu.context.x[5] = r.cpu.context.x[13] = 0x3000;
    r.cpu.context.x[11] = 0x12345678;
    r.cpu.context.x[12] = 0x98765432;
    CHECK(r.Step(1).stop == CpuStop::Running);
    CHECK(r.cpu.context.x[3] == 0xaaaabbbb);
    CHECK(r.cpu.context.x[4] == 0xccccdddd);
    CHECK(r.memory.Write8(0x4007, 0x7f));
    CHECK(r.Step(3).stop == CpuStop::Running);
    CHECK(r.cpu.context.x[10] == 1);
    return true;
}
bool TestExclusiveX() {
    Fixture r;
    CHECK(r.memory.Write64(0x3000, UINT64_C(0x0123456789abcdef)));
    CHECK(r.memory.Write64(0x3008, UINT64_C(0xfedcba9876543210)));
    r.cpu.context.x[2] = r.cpu.context.x[9] = 0x3000;
    r.cpu.context.x[7] = UINT64_C(0x2233445566778899);
    r.cpu.context.x[8] = UINT64_C(0x99aabbccddeeff00);
    CHECK(r.Step(4).stop == CpuStop::Running);
    CHECK(r.cpu.context.x[0] == UINT64_C(0x0123456789abcdef));
    CHECK(r.cpu.context.x[1] == UINT64_C(0xfedcba9876543210));
    CHECK(r.Step(6).stop == CpuStop::Running && r.cpu.context.x[6] == 0);
    CHECK(r.memory.Read64(0x3000) == UINT64_C(0x2233445566778899));
    CHECK(r.memory.Read64(0x3008) == UINT64_C(0x99aabbccddeeff00));

    // A 64-bit pair must be 16-byte aligned and the fault cannot alter Rt.
    r.cpu.context.x[5] = 0x3008;
    r.cpu.context.x[3] = 97;
    r.cpu.context.x[4] = 98;
    CHECK(r.Step(5).stop == CpuStop::DataFault);
    CHECK(r.cpu.context.pc == 0x1014);
    CHECK(r.cpu.context.x[3] == 97 && r.cpu.context.x[4] == 98);
    r.cpu.context.x[5] = 0x3000;
    r.cpu.context.x[13] = 0x3000;
    r.cpu.context.x[11] = 51;
    r.cpu.context.x[12] = 52;
    CHECK(r.Step(5).stop == CpuStop::Running);
    CHECK(r.Step(7).stop == CpuStop::Running && r.cpu.context.x[10] == 0);
    CHECK(r.memory.Read64(0x3000) == 51 && r.memory.Read64(0x3008) == 52);
    return true;
}
bool TestCASP() {
    for (unsigned i = 8; i < 16; ++i) {
        const auto op = kPairWords[i];
        const unsigned width = ((op >> 30) & 1U) ? 8U : 4U;
        const unsigned rs = (op >> 16) & 31U;
        const unsigned rt = op & 31U;
        const unsigned rn = (op >> 5) & 31U;
        const auto mask = width == 8 ? UINT64_MAX : UINT64_C(0xffffffff);
        for (bool match : {true, false}) {
            Fixture r;
            const auto old_first = UINT64_C(0x0123456789abcdef) & mask;
            const auto old_second = UINT64_C(0xfedcba9876543210) & mask;
            const auto desired_first = UINT64_C(0xaabbccddeeff0011) & mask;
            const auto desired_second = UINT64_C(0x2233445566778899) & mask;
            if (width == 8) {
                CHECK(r.memory.Write64(0x3000, old_first));
                CHECK(r.memory.Write64(0x3008, old_second));
            } else {
                CHECK(r.memory.Write32(0x3000, static_cast<std::uint32_t>(old_first)));
                CHECK(r.memory.Write32(0x3004, static_cast<std::uint32_t>(old_second)));
            }
            r.cpu.context.x[rn] = 0x3000;
            r.cpu.context.x[rs] = match ? old_first : old_first ^ 1U;
            r.cpu.context.x[rs + 1] = old_second;
            r.cpu.context.x[rt] = desired_first;
            r.cpu.context.x[rt + 1] = desired_second;
            r.cpu.context.nzcv = 0xb;
            const auto stamp = r.memory.LoadExclusivePair(0x3000, width);
            CHECK(stamp.has_value());
            CHECK(r.Step(i).stop == CpuStop::Running);
            CHECK(r.cpu.context.x[rs] == old_first);
            CHECK(r.cpu.context.x[rs + 1] == old_second);
            const auto first = r.memory.AtomicComparePair(
                0x4000, width, UINT64_MAX, UINT64_MAX, 0, 0);
            CHECK(first.has_value());
            CHECK(first->first == (match ? desired_first : old_first));
            CHECK(first->second == (match ? desired_second : old_second));
            CHECK(r.cpu.context.nzcv == 0xb);
            CHECK(r.cpu.context.pc == 0x1004 + 4 * i);
            CHECK(r.memory.StoreExclusivePair(
                0x3000, width, 0, 0, stamp->frame_id, stamp->generation) ==
                (match ? GuestMemory::ExclusiveStore::ReservationLost :
                         GuestMemory::ExclusiveStore::Stored));
        }
    }
    return true;
}
bool TestFaults() {
    for (unsigned i : {0U, 4U, 8U, 12U}) {
        Fixture r;
        const unsigned rn = (kPairWords[i] >> 5) & 31U;
        const unsigned w = i < 8 ? (i < 4 ? 4U : 8U) :
                                    (i < 12 ? 4U : 8U);
        r.cpu.context.x[rn] = 0x3000 + (2 * w);
        CHECK(r.Step(i).stop == CpuStop::Running); // properly aligned pair
        r.cpu.context.x[rn] = 0x3000 + w;
        const auto fault = r.Step(i);
        CHECK(fault.stop == CpuStop::DataFault);
        CHECK(r.cpu.context.pc == 0x1000 + 4 * i);
    }
    {
        Fixture r;
        CHECK(r.memory.Protect(0x3000, 4096, Access::Read));
        CHECK(r.memory.Protect(0x4000, 4096, Access::Read));
        r.cpu.context.x[4] = 0x3000;
        r.cpu.context.x[0] = 0;
        r.cpu.context.x[1] = 0;
        CHECK(r.Step(12).stop == CpuStop::DataFault);
        CHECK(r.cpu.context.pc == 0x1030);
        CHECK(r.cpu.context.x[0] == 0 && r.cpu.context.x[1] == 0);
    }
    {
        Fixture r;
        r.cpu.context.sp = 0x3008;
        // Transform the independently assembled LDXP X0,X1,[X2] to [SP].
        CHECK(r.Dynamic((kPairWords[4] & ~0x3e0U) | 0x3e0U).stop == CpuStop::DataFault);
        CHECK(r.cpu.context.pc == 0x2000);
    }
    {
        Fixture r;
        // CASP register pairs must start on even registers.
        CHECK(r.Dynamic(kPairWords[12] | (1U << 16)).stop ==
              CpuStop::UndefinedInstruction);
        CHECK(r.cpu.context.pc == 0x2000);
    }
    return true;
}
} // namespace

int main() {
    CHECK(TestExclusiveW());
    CHECK(TestExclusiveX());
    CHECK(TestCASP());
    CHECK(TestFaults());
    std::cout << "AArch64 exclusive pairs and CASP:"
                 " 16 original operations, aliases, reserve stamps, faults passed\n";
    return EXIT_SUCCESS;
}
