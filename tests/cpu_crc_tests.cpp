#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include "nint2/cpu.hpp"

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "AArch64 CRC failure: " #expr << " at line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::Access;
using nint2::CpuInterpreter;
using nint2::CpuStop;
using nint2::GuestMemory;

// Independently assembled via Clang AArch64, original_crc.s. The source
// parity checker compares all eleven words, including zero-register cases.
constexpr std::array<std::uint32_t, 11> kCrcWords = {
    0x1ac24020, 0x1ac54483, 0x1ac848e6, 0x9acb4d49,
    0x1ace51ac, 0x1ad1560f, 0x1ad45a72, 0x9ad75ed5,
    0x1adf43ff, 0x9adf5fff, 0xd65f03c0
};

// Independent reference: calculate a per-byte table from the polynomial,
// then consume the instruction's source operand in little-endian order.
std::uint32_t Reference(std::uint32_t current, std::uint64_t input,
                        unsigned bytes, bool castagnoli) {
    const std::uint32_t polynomial =
        castagnoli ? 0x82F63B78U : 0xEDB88320U;
    std::array<std::uint32_t, 256> table{};
    for (unsigned n = 0; n < table.size(); ++n) {
        std::uint32_t value = n;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value & 1U) ? (value >> 1) ^ polynomial : value >> 1;
        table[n] = value;
    }
    for (unsigned n = 0; n < bytes; ++n) {
        current = (current >> 8) ^
                  table[(current ^ static_cast<std::uint8_t>(input)) & 0xff];
        input >>= 8;
    }
    return current;
}

struct Fixture {
    GuestMemory memory{0x8000};
    CpuInterpreter cpu{};
    Fixture() {
        if (!memory.Map(0x1000, 4096, Access::Read | Access::Write))
            std::exit(EXIT_FAILURE);
        for (std::size_t i = 0; i < kCrcWords.size(); ++i)
            if (!memory.Write32(0x1000 + i * 4, kCrcWords[i]))
                std::exit(EXIT_FAILURE);
        if (!memory.Protect(0x1000, 4096, Access::Read | Access::Execute))
            std::exit(EXIT_FAILURE);
    }
    [[nodiscard]] nint2::CpuResult Run(unsigned index) {
        cpu.context.pc = 0x1000 + 4 * index;
        return cpu.Step(memory);
    }
};
} // namespace

int main() {
    // An empty byte with initial CRC zero yields zero for both polynomials.
    CHECK(Reference(0, 0, 1, false) == 0);
    CHECK(Reference(0, 0, 1, true) == 0);
    // Standard '123456789' CRC vectors: the ARM instruction does not apply
    // the conventional initial/final XOR, so the test does it explicitly.
    {
        std::uint32_t crc = UINT32_MAX, crcc = UINT32_MAX;
        constexpr std::string_view input = "123456789";
        for (unsigned char c : input) {
            crc = Reference(crc, c, 1, false);
            crcc = Reference(crcc, c, 1, true);
        }
        CHECK(~crc == UINT32_C(0xcbf43926));
        CHECK(~crcc == UINT32_C(0xe3069283));
    }

    // Check both polynomials, four operand widths, X-register behavior,
    // 1,024 deterministic vectors per instruction and all NZCV combinations.
    std::uint32_t random = 0x86e7a05bU;
    for (unsigned index = 0; index < 8; ++index) {
        const auto op = kCrcWords[index];
        const unsigned opcode = (op >> 10) & 63U;
        const unsigned width = 1U << (opcode & 3U);
        const bool castagnoli = (opcode & 4U) != 0;
        const unsigned rd = op & 31U;
        const unsigned rn = (op >> 5) & 31U;
        const unsigned rm = (op >> 16) & 31U;
        for (unsigned trial = 0; trial < 1024; ++trial) {
            Fixture fixture;
            random = 1664525U * random + 1013904223U;
            const auto value = (std::uint64_t{random} << 32) | random;
            random = 1664525U * random + 1013904223U;
            const auto seed = random;
            fixture.cpu.context.x[rn] = UINT64_C(0xabcd000000000000) | seed;
            fixture.cpu.context.x[rm] = value;
            fixture.cpu.context.x[rd] = UINT64_MAX;
            fixture.cpu.context.nzcv = static_cast<std::uint8_t>(trial & 15U);
            const auto before = fixture.cpu.context.nzcv;
            const auto expected = Reference(seed, value, width, castagnoli);
            const auto result = fixture.Run(index);
            CHECK(result.stop == CpuStop::Running);
            CHECK(result.steps == 1 && result.instruction == op);
            CHECK(fixture.cpu.context.pc == 0x1000 + 4 * index + 4);
            CHECK(fixture.cpu.context.x[rd] == expected);
            CHECK(fixture.cpu.context.nzcv == before);
        }
    }

    // CRC32X/CRC32CX must process the high half of Xm, not truncate to Wm.
    for (unsigned index : {3U, 7U}) {
        Fixture fixture;
        const auto opcode = kCrcWords[index];
        const unsigned rn = (opcode >> 5) & 31U;
        const unsigned rm = (opcode >> 16) & 31U;
        const unsigned rd = opcode & 31U;
        fixture.cpu.context.x[rn] = 0;
        fixture.cpu.context.x[rm] = UINT64_C(0xfeedface00000000);
        CHECK(fixture.Run(index).stop == CpuStop::Running);
        CHECK(fixture.cpu.context.x[rd] ==
              Reference(0, UINT64_C(0xfeedface00000000), 8, index == 7));
    }

    // Unallocated SF/size combinations must trap, leaving PC and registers
    // intact; no arithmetic decoder may accidentally accept these opcodes.
    for (unsigned index = 0; index < 8; ++index) {
        Fixture fixture;
        const auto original = kCrcWords[index];
        const bool wide = (original >> 31) != 0;
        const auto illegal = wide ? original & ~(1U << 31) : original | (1U << 31);
        GuestMemory mem(0x8000);
        CHECK(mem.Map(0x1000, 4096, Access::Read | Access::Write));
        CHECK(mem.Write32(0x1000, illegal));
        CHECK(mem.Protect(0x1000, 4096, Access::Read | Access::Execute));
        const auto old_regs = fixture.cpu.context.x;
        const auto old_flags = fixture.cpu.context.nzcv;
        fixture.cpu.context.pc = 0x1000;
        const auto result = fixture.cpu.Step(mem);
        CHECK(result.stop == CpuStop::UndefinedInstruction);
        CHECK(result.instruction == illegal);
        CHECK(fixture.cpu.context.pc == 0x1000);
        CHECK(fixture.cpu.context.x == old_regs);
        CHECK(fixture.cpu.context.nzcv == old_flags);
    }

    // Encoding WZR/XZR as source or destination is valid. Writes to WZR
    // disappear without mutating the stored register bank or condition flags.
    for (unsigned index : {8U, 9U}) {
        Fixture fixture;
        fixture.cpu.context.nzcv = 15;
        const auto before = fixture.cpu.context.x;
        CHECK(fixture.Run(index).stop == CpuStop::Running);
        CHECK(fixture.cpu.context.x == before);
        CHECK(fixture.cpu.context.nzcv == 15);
    }

    // Known public CRC check values must also be produced by the INTERPRETER,
    // not only by the independent reference implementation above.
    for (bool castagnoli : {false, true}) {
        Fixture fixture;
        const unsigned index = castagnoli ? 4U : 0U;
        const std::uint32_t instruction = kCrcWords[index];
        const unsigned accumulator = (instruction >> 5) & 31U;
        const unsigned operand = (instruction >> 16) & 31U;
        const unsigned destination = instruction & 31U;
        std::uint32_t running = UINT32_MAX;
        for (unsigned char c : std::string_view{"123456789"}) {
            fixture.cpu.context.x[accumulator] = running;
            fixture.cpu.context.x[operand] = c;
            CHECK(fixture.Run(index).stop == CpuStop::Running);
            running = static_cast<std::uint32_t>(fixture.cpu.context.x[destination]);
        }
        CHECK(~running == (castagnoli ? UINT32_C(0xe3069283) :
                                        UINT32_C(0xcbf43926)));
    }

    // AArch64 permits source and destination register overlap. The decoder
    // must read the old accumulator/data before writing Rd.
    {
        GuestMemory memory(0x8000);
        CHECK(memory.Map(0x2000, 4096, Access::Read | Access::Write));
        constexpr unsigned reg = 7;
        const std::uint32_t alias =
            (kCrcWords[2] & ~((31U << 16) | (31U << 5) | 31U)) |
            (reg << 16) | (reg << 5) | reg;
        CHECK(memory.Write32(0x2000, alias));
        CHECK(memory.Protect(0x2000, 4096, Access::Read | Access::Execute));
        CpuInterpreter cpu;
        cpu.context.pc = 0x2000;
        cpu.context.nzcv = 0xe;
        cpu.context.x[reg] = UINT64_C(0xfedcba9812345678);
        const auto old = static_cast<std::uint32_t>(cpu.context.x[reg]);
        const auto expected = Reference(old, old, 4, false);
        CHECK(cpu.Step(memory).stop == CpuStop::Running);
        CHECK(cpu.context.x[reg] == expected);
        CHECK(cpu.context.nzcv == 0xe);
    }

    // The complete source-authored CRC32/CRC32C class does not touch guest
    // memory other than fetching the instruction; only Rd and PC change.
    std::cout << "AArch64 CRC32/CRC32C: 8192 checked vectors, known IEEE/Castagnoli"
                 " digests, illegal encodings and zero-register cases passed\n";
    return EXIT_SUCCESS;
}
