#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include "nint2/cpu.hpp"

namespace {
using nint2::Access;
using nint2::CpuInterpreter;
using nint2::CpuStop;
using nint2::GuestMemory;

// Independent 42-word Clang AArch64 corpus from original_lse.s.
// The assembler-parity CI script compares the exact machine code.
constexpr std::array<std::uint32_t, 42> kSourceWords = {
    0x08a07c41, 0x08e37ca4, 0x08a6fd07, 0x08e9fd6a,
    0x48a07c41, 0x88a07c41, 0xc8a07c41, 0xc8e37ca4,
    0xc8a6fd07, 0xc8e9fd6a, 0x38208041, 0x38a380a4,
    0x38668107, 0x38e9816a, 0x78208041, 0xb8208041,
    0xf8208041, 0xf8a380a4, 0xf8668107, 0xf8e9816a,
    0x38200041, 0x38a300a4, 0x38660107, 0x38e9016a,
    0x78200041, 0xb8200041, 0xf8200041, 0xf8a300a4,
    0xf8660107, 0xf8e9016a, 0x38201041, 0x38202041,
    0x38203041, 0xb8204041, 0xb8205041, 0xb8206041,
    0xb8207041, 0xc8dffc20, 0xc89ffc20, 0x08dffc20,
    0x089ffc20, 0xd65f03c0
};
void Check(bool okay, const char* label, unsigned line) {
    if (!okay) {
        std::cerr << "LSE regression: " << label << " at line " << line << '\n';
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(x) Check(static_cast<bool>(x), #x, __LINE__)

std::uint64_t Mask(unsigned size) {
    return size == 8 ? UINT64_MAX : (std::uint64_t{1} << (size * 8)) - 1;
}
std::uint64_t Load(const GuestMemory& m, std::uint64_t a, unsigned bytes) {
    if (bytes == 1) return m.Read8(a).value();
    if (bytes == 2) return m.Read16(a).value();
    if (bytes == 4) return m.Read32(a).value();
    return m.Read64(a).value();
}
bool Store(GuestMemory& m, std::uint64_t a, unsigned bytes, std::uint64_t v) {
    if (bytes == 1) return m.Write8(a, static_cast<std::uint8_t>(v));
    if (bytes == 2) return m.Write16(a, static_cast<std::uint16_t>(v));
    if (bytes == 4) return m.Write32(a, static_cast<std::uint32_t>(v));
    return m.Write64(a, v);
}
struct Rig {
    GuestMemory mem{0x10000};
    CpuInterpreter cpu{};
    Rig() {
        CHECK(mem.Map(0x1000, 0x1000, Access::Read | Access::Write));
        CHECK(mem.Map(0x3000, 0x1000, Access::Read | Access::Write));
        for (unsigned i = 0; i < kSourceWords.size(); ++i)
            CHECK(mem.Write32(0x1000 + 4 * i, kSourceWords[i]));
        CHECK(mem.Protect(0x1000, 0x1000, Access::Read | Access::Execute));
        CHECK(mem.Fetch32(0x1000) == kSourceWords[0]);
    }
    [[nodiscard]] nint2::CpuResult Step(unsigned index) {
        cpu.context.pc = 0x1000 + index * 4;
        return cpu.Step(mem);
    }
    [[nodiscard]] nint2::CpuResult Dynamic(std::uint32_t op) {
        CHECK(mem.Map(0x2000, 0x1000, Access::Read | Access::Write));
        CHECK(mem.Write32(0x2000, op));
        CHECK(mem.Protect(0x2000, 0x1000, Access::Read | Access::Execute));
        cpu.context.pc = 0x2000;
        return cpu.Step(mem);
    }
};
void TestCAS() {
    for (unsigned index = 0; index < 10; ++index) {
        const auto op = kSourceWords[index];
        const unsigned size = 1U << ((op >> 30) & 3U);
        const unsigned rs = (op >> 16) & 31U;
        const unsigned rt = op & 31U;
        const unsigned rn = (op >> 5) & 31U;
        const auto mask = Mask(size);
        for (bool equal : {false, true}) {
            Rig r;
            const auto old = mask & UINT64_C(0x8877665544332211);
            const auto desired = mask & UINT64_C(0x1234567890abcdef);
            CHECK(Store(r.mem, 0x3000, size, old));
            r.cpu.context.x[rn] = 0x3000;
            r.cpu.context.x[rs] = equal ? old : old + 1;
            r.cpu.context.x[rt] = desired;
            r.cpu.context.nzcv = 13;
            const auto reservation = r.mem.LoadExclusive(0x3000, size);
            CHECK(reservation.has_value());
            CHECK(r.Step(index).stop == CpuStop::Running);
            CHECK(r.cpu.context.pc == 0x1004 + 4 * index);
            CHECK(r.cpu.context.x[rs] == old);
            CHECK(Load(r.mem, 0x3000, size) == (equal ? desired : old));
            CHECK(r.cpu.context.nzcv == 13);
            const auto previous = r.mem.StoreExclusive(
                0x3000, size, 0, reservation->frame_id, reservation->generation);
            CHECK(previous == (equal ? GuestMemory::ExclusiveStore::ReservationLost :
                                       GuestMemory::ExclusiveStore::Stored));
        }
    }
}
std::uint64_t ReferenceRmw(unsigned kind, unsigned size,
                           std::uint64_t lhs, std::uint64_t rhs) {
    const auto mask = Mask(size);
    lhs &= mask; rhs &= mask;
    const auto sign = std::uint64_t{1} << (size * 8 - 1);
    switch (kind) {
    case 0: return (lhs + rhs) & mask;
    case 1: return lhs & ~rhs & mask;
    case 2: return (lhs ^ rhs) & mask;
    case 3: return (lhs | rhs) & mask;
    case 4: return (lhs ^ sign) >= (rhs ^ sign) ? lhs : rhs;
    case 5: return (lhs ^ sign) <= (rhs ^ sign) ? lhs : rhs;
    case 6: return lhs >= rhs ? lhs : rhs;
    case 7: return lhs <= rhs ? lhs : rhs;
    default: return rhs;
    }
}
void TestRmw() {
    for (unsigned index = 10; index <= 36; ++index) {
        const auto op = kSourceWords[index];
        const unsigned size = 1U << ((op >> 30) & 3U);
        const unsigned rn = (op >> 5) & 31U;
        const unsigned rs = (op >> 16) & 31U;
        const unsigned rt = op & 31U;
        const unsigned kind = (op & (1U << 15)) ? 8 : (op >> 12) & 7U;
        for (unsigned trial = 0; trial < 3; ++trial) {
            Rig r;
            const auto old = trial == 0 ? Mask(size) - 1 :
                             trial == 1 ? UINT64_C(0x12345678abcdef11) & Mask(size) : 0;
            const auto input = trial == 0 ? 5U : trial == 1 ? 0x37U : Mask(size);
            CHECK(Store(r.mem, 0x3000, size, old));
            r.cpu.context.x[rn] = 0x3000;
            r.cpu.context.x[rs] = input;
            r.cpu.context.x[rt] = UINT64_MAX;
            r.cpu.context.nzcv = 11;
            CHECK(r.Step(index).stop == CpuStop::Running);
            CHECK(r.cpu.context.x[rt] == old);
            CHECK(Load(r.mem, 0x3000, size) ==
                  ReferenceRmw(kind, size, old, input));
            CHECK(r.cpu.context.nzcv == 11);
        }
    }
}
void TestOrderedAccess() {
    for (unsigned index : {37U, 39U}) {
        Rig r;
        const auto op = kSourceWords[index];
        const unsigned size = 1U << ((op >> 30) & 3U);
        CHECK(Store(r.mem, 0x3000, size, UINT64_C(0xfedcba9876543210)));
        r.cpu.context.x[1] = 0x3000;
        r.cpu.context.x[0] = 0x1111111111111111ULL;
        CHECK(r.Step(index).stop == CpuStop::Running);
        CHECK(r.cpu.context.x[0] ==
              (UINT64_C(0xfedcba9876543210) & Mask(size)));
    }
    for (unsigned index : {38U, 40U}) {
        Rig r;
        const auto op = kSourceWords[index];
        const unsigned size = 1U << ((op >> 30) & 3U);
        r.cpu.context.x[1] = 0x3000;
        r.cpu.context.x[0] = UINT64_C(0x8899aabbccddeeff);
        CHECK(r.Step(index).stop == CpuStop::Running);
        CHECK(Load(r.mem, 0x3000, size) ==
              (UINT64_C(0x8899aabbccddeeff) & Mask(size)));
    }
}
void TestPermissionsAndFaults() {
    for (unsigned index : {0U, 6U, 10U, 16U, 20U, 26U, 37U, 38U}) {
        const auto op = kSourceWords[index];
        const unsigned rn = (op >> 5) & 31U;
        const unsigned size = 1U << ((op >> 30) & 3U);
        for (unsigned mode = 0; mode < 4; ++mode) {
            Rig r;
            if (mode == 0) CHECK(r.mem.Protect(0x3000, 0x1000, Access::Read));
            if (mode == 1) CHECK(r.mem.Protect(0x3000, 0x1000, Access::Write));
            if (mode == 2) CHECK(r.mem.Unmap(0x3000, 0x1000));
            r.cpu.context.x[rn] =
                mode == 3 ? 0x3000 + (size == 1 ? 0 : 1) : 0x3000;
            const auto pc = 0x1000 + 4 * index;
            const auto result = r.Step(index);
            const bool load = index == 37;
            const bool store = index == 38;
            const bool allowed = (load && mode == 0) || (store && mode == 1) ||
                                 (mode == 3 && size == 1);
            CHECK((result.stop == CpuStop::Running) == allowed);
            if (!allowed) {
                CHECK(result.stop == CpuStop::DataFault);
                CHECK(r.cpu.context.pc == pc);
            }
        }
    }
    {
        Rig r;
        r.cpu.context.sp = 0x3008;
        CHECK(r.Dynamic(0xc8dfffe0).stop == CpuStop::DataFault);
        CHECK(r.cpu.context.pc == 0x2000);
    }
    {
        Rig r;
        r.cpu.context.x[2] = 0x3000;
        CHECK(r.Dynamic(0x38209041).stop == CpuStop::UndefinedInstruction);
        CHECK(r.cpu.context.pc == 0x2000);
    }
    {
        Rig r;
        r.cpu.context.x[2] = 0x3001;
        CHECK(r.mem.Write64(0x3000, 0x12345678));
        CHECK(r.Dynamic(0xf8208041).stop == CpuStop::DataFault);
        CHECK(r.mem.Read64(0x3000) == UINT64_C(0x12345678));
    }
}
void TestAliasesAndZeroRegisters() {
    {
        Rig r;
        CHECK(r.mem.Alias(0x5000, 0x3000, 0x1000, Access::Read | Access::Write));
        CHECK(r.mem.Write64(0x3000, 0x76543210));
        const auto stamp = r.mem.LoadExclusive(0x3000, 8);
        CHECK(stamp.has_value());
        r.cpu.context.x[2] = 0x5000;
        r.cpu.context.x[0] = 1;
        CHECK(r.Step(16).stop == CpuStop::Running);
        CHECK(r.mem.Read64(0x3000) == r.mem.Read64(0x5000));
        CHECK(r.mem.StoreExclusive(0x3000, 8, 1, stamp->frame_id,
                                   stamp->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
    }
    {
        Rig r;
        CHECK(r.mem.Write32(0x3000, 10));
        r.cpu.context.x[0] = 7;
        r.cpu.context.x[2] = 0x3000;
        CHECK(r.Dynamic(0xb820005f).stop == CpuStop::Running);
        CHECK(r.mem.Read32(0x3000) == 17);
    }
    {
        Rig r;
        r.cpu.context.x[2] = 0x3000;
        r.cpu.context.x[1] = 42;
        CHECK(r.Dynamic(0xc8bf7c41).stop == CpuStop::Running);
        CHECK(r.mem.Read64(0x3000) == 42);
    }
}
void TestDeterministicSweep() {
    std::uint32_t random = 0x827316f1U;
    for (unsigned n = 0; n < 512; ++n) {
        random = random * 1664525U + 1013904223U;
        const unsigned index = 10 + random % 27;
        const auto op = kSourceWords[index];
        const unsigned size = 1U << ((op >> 30) & 3U);
        const unsigned rn = (op >> 5) & 31U;
        const unsigned rs = (op >> 16) & 31U;
        const unsigned rt = op & 31U;
        const unsigned kind = (op & (1U << 15)) ? 8U : (op >> 12) & 7U;
        Rig r;
        random = random * 1664525U + 1013904223U;
        const auto old =
            (UINT64_C(0xaab0fc0ff00dcafe) ^ (std::uint64_t{random} << 13)) & Mask(size);
        random = random * 1664525U + 1013904223U;
        const auto input =
            (UINT64_C(0x0011223344556677) ^ (std::uint64_t{random} << 19)) & Mask(size);
        CHECK(Store(r.mem, 0x3000, size, old));
        r.cpu.context.x[rn] = 0x3000;
        r.cpu.context.x[rs] = input;
        r.cpu.context.x[rt] = 0xdeadbeef;
        CHECK(r.Step(index).stop == CpuStop::Running);
        CHECK(Load(r.mem, 0x3000, size) ==
              ReferenceRmw(kind, size, old, input));
        CHECK(r.cpu.context.x[rt] == old);
    }
}
} // namespace
int main() {
    TestCAS();
    TestRmw();
    TestOrderedAccess();
    TestPermissionsAndFaults();
    TestAliasesAndZeroRegisters();
    TestDeterministicSweep();
    std::cout << "AArch64 LSE CAS/SWP/RMW + LDAR/STLR:"
                 " widths, barriers, faults, aliases and 512 vectors passed\n";
    return EXIT_SUCCESS;
}
