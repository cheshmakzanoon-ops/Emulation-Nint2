#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " #condition << " at line " << __LINE__ << '\n'; \
    return false; } } while (false)

namespace {
constexpr std::uint64_t kBase = 0x1000;
// Original fixture tests/fixtures/original_sp_logical.s. Verify its encodings
// independently with the AArch64 assembler before changing the instruction words.
constexpr std::array<std::uint32_t, 17> words = {
    0xb10043e0, 0x710007e1, 0xf10083ff, 0x310007ff,
    0x927cec5f, 0x32001c7f, 0xd2089c9f, 0xf2401cbf,
    0xaac703e6, 0x0aca7d28, 0xcaed458b, 0x6af021ee,
    0x927cefff, 0xaac703ff, 0xd65f03c0, 0x8bc20020,
    0x2ac28020
};

bool ArithmeticSp(nint2::GuestMemory& memory) {
    // All ADD/SUB immediate forms read SP, even when flags are enabled;
    // flag-setting destination register 31 still means ZR, never SP.
    constexpr std::array<std::uint64_t, 4> inputs = {
        0xfffffffffffffff0ULL, 0x1111111180000000ULL,
        32, 0x12345678ffffffffULL
    };
    constexpr std::array<std::uint8_t, 4> flags = {6, 3, 6, 6};
    for (unsigned i = 0; i < inputs.size(); ++i) {
        nint2::CpuInterpreter cpu;
        cpu.context.pc = kBase + 4 * i;
        cpu.context.sp = inputs[i];
        cpu.context.x.fill(0xcafebabedeadbeefULL);
        const auto original = cpu.context;
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        CHECK(cpu.context.sp == original.sp);
        CHECK(cpu.context.pc == original.pc + 4);
        CHECK(cpu.context.nzcv == flags[i]);
        if (i == 0) CHECK(cpu.context.x[0] == 0);
        if (i == 1) CHECK(cpu.context.x[1] == 0x7fffffffULL);
        for (unsigned reg = 0; reg < cpu.context.x.size(); ++reg)
            if (reg != i || i > 1) CHECK(cpu.context.x[reg] == original.x[reg]);
    }
    return true;
}

bool LogicalSp(nint2::GuestMemory& memory) {
    nint2::CpuInterpreter cpu;
    cpu.context.sp = 0x3ff0;
    cpu.context.x[2] = 0x123456789abcdef7ULL;
    cpu.context.x[3] = 0xfedcba9887654300ULL;
    cpu.context.x[4] = 0x0123456789abcdefULL;
    cpu.context.x[5] = 0x100;
    cpu.context.nzcv = 3;
    const auto original = cpu.context.x;
    cpu.context.pc = kBase + 4 * 4;
    CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
    CHECK(cpu.context.sp == 0x123456789abcdef0ULL);
    CHECK(cpu.context.nzcv == 3 && cpu.context.x == original);
    CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
    CHECK(cpu.context.sp == 0x876543ffULL); // WSP writes clear the upper 32 bits.
    CHECK(cpu.context.nzcv == 3 && cpu.context.x == original);
    CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
    CHECK(cpu.context.sp == 0xfe23ba6776ab32efULL);
    CHECK(cpu.context.nzcv == 3 && cpu.context.x == original);
    CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
    CHECK(cpu.context.sp == 0xfe23ba6776ab32efULL); // ANDS rd=31 means XZR.
    CHECK(cpu.context.nzcv == 4 && cpu.context.x == original);
    cpu.context.pc = kBase + 4 * 12;
    CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
    CHECK(cpu.context.sp == 0); // Logical-immediate source register 31 is ZR.
    CHECK(cpu.context.nzcv == 4 && cpu.context.x == original);
    return true;
}

bool LogicalRotate(nint2::GuestMemory& memory) {
    // C++20 std::rotr is independent of the interpreter's shift helper.
    // Exercise zero, maximum W rotation, an X inverted operand, and W NZCV.
    std::uint64_t state = 0xa5a5c3c3deadbeefULL;
    for (unsigned vector = 0; vector < 1024; ++vector) {
        state = state * 6364136223846793005ULL + 1;
        const std::uint64_t a = state;
        state = state * 6364136223846793005ULL + 1;
        const std::uint64_t b = vector == 0 ? 0 : state;
        nint2::CpuInterpreter cpu;
        cpu.context.sp = 0x3ff0;
        cpu.context.nzcv = 3;
        auto& x = cpu.context.x;
        x[7] = b;
        x[9] = a; x[10] = b;
        x[12] = a; x[13] = b;
        x[15] = a; x[16] = b;
        cpu.context.pc = kBase + 4 * 8;
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        CHECK(x[6] == b && cpu.context.nzcv == 3);
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        CHECK(x[8] == (static_cast<std::uint32_t>(a) &
                       std::rotr(static_cast<std::uint32_t>(b), 31)));
        CHECK(cpu.context.nzcv == 3);
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        CHECK(x[11] == (a ^ ~std::rotr(b, 17)) && cpu.context.nzcv == 3);
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        const auto expected = static_cast<std::uint32_t>(a) &
                              ~std::rotr(static_cast<std::uint32_t>(b), 8);
        CHECK(x[14] == expected);
        CHECK(cpu.context.nzcv == ((expected >> 31) * 8 + (expected == 0 ? 4 : 0)));
        CHECK(cpu.context.sp == 0x3ff0);
        cpu.context.pc = kBase + 4 * 13;
        const auto previous = cpu.context;
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::Running);
        CHECK(cpu.context.sp == previous.sp && cpu.context.x == previous.x);
        CHECK(cpu.context.nzcv == previous.nzcv);
    }
    // ROR is allocated for logical shifted operands, but still unallocated
    // for ADD/SUB shifted operands; W forms cannot encode shifts >= 32.
    for (unsigned i = 15; i < words.size(); ++i) {
        nint2::CpuInterpreter cpu;
        cpu.context.pc = kBase + 4 * i;
        cpu.context.sp = 0x3ff0;
        cpu.context.x.fill(UINT64_MAX);
        cpu.context.nzcv = 7;
        const auto original = cpu.context;
        CHECK(cpu.Step(memory).stop == nint2::CpuStop::UndefinedInstruction);
        CHECK(cpu.context.pc == original.pc && cpu.context.sp == original.sp);
        CHECK(cpu.context.x == original.x && cpu.context.nzcv == original.nzcv);
    }
    return true;
}
} // namespace

int main() {
    using nint2::Access;
    nint2::GuestMemory memory(0x10000);
    if (!memory.Map(kBase, 4096, Access::Read | Access::Write)) return EXIT_FAILURE;
    for (unsigned i = 0; i < words.size(); ++i)
        if (!memory.Write32(kBase + 4 * i, words[i])) return EXIT_FAILURE;
    if (!memory.Protect(kBase, 4096, Access::Read | Access::Execute)) return EXIT_FAILURE;
    bool passed = ArithmeticSp(memory);
    passed = LogicalSp(memory) && passed;
    passed = LogicalRotate(memory) && passed;
    if (!passed) return EXIT_FAILURE;
    std::cout << "AArch64 SP/ZR operand rules and 4096 logical ROR vectors passed\n";
    return EXIT_SUCCESS;
}
