#include "nint2/arena.hpp"
#include "nint2/elf.hpp"
#include "nint2/session.hpp"
#include "nint2/scheduler.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#define REQUIRE(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using namespace nint2;
using nint2::vm::GuestArena;
constexpr std::uint64_t kArenaBase = GuestArena::kBase;
constexpr std::uint64_t kPage = GuestMemory::kPageSize;

// Assembled with Clang's original AArch64 target from original_vm_guest.s.
// The assembler parity script checks all 46 words independently in CI.
constexpr std::array<std::uint32_t, 46> kWords{
    0xd2800248, 0xd2820000, 0xd2800061, 0xd2800002, 0xd4000001,
    0xf100001f, 0x540004ab, 0xaa0003f3, 0x52800ac1, 0x39000261,
    0x528009a1, 0x39000661, 0x52800421, 0x39000a61, 0x52800141,
    0x39000e61, 0xd2800028, 0xd2800020, 0xaa1303e1, 0xd2800082,
    0xd4000001, 0xf100101f, 0x540002a1, 0xd2800288, 0xaa1303e0,
    0xd28000a1, 0xd4000001, 0xf100001f, 0x540001e1, 0xd28002a8,
    0xaa1303e0, 0xaa1403e1, 0xd4000001, 0xf100001f, 0x54000121,
    0xd2800268, 0xaa1303e0, 0xd4000001, 0xf100001f, 0x54000081,
    0xd2800048, 0xd2800000, 0xd4000001, 0xd2800048, 0xd28009a0,
    0xd4000001
};

void WriteLittle(std::vector<std::uint8_t>& image, std::size_t offset,
                 std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        image[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> GuestElf() {
    constexpr std::size_t kPayload = 0x100;
    std::vector<std::uint8_t> result(kPayload + kWords.size() * 4);
    result[0] = 0x7f; result[1] = 'E'; result[2] = 'L'; result[3] = 'F';
    result[4] = 2; result[5] = 1; result[6] = 1;
    WriteLittle(result, 16, 2, 2);  // ET_EXEC
    WriteLittle(result, 18, 183, 2); // EM_AARCH64
    WriteLittle(result, 20, 1, 4);
    WriteLittle(result, 24, 0x10000, 8);
    WriteLittle(result, 32, 64, 8);
    WriteLittle(result, 52, 64, 2);
    WriteLittle(result, 54, 56, 2);
    WriteLittle(result, 56, 1, 2);
    WriteLittle(result, 64, 1, 4); // PT_LOAD
    WriteLittle(result, 68, 5, 4); // PF_R + PF_X
    WriteLittle(result, 72, kPayload, 8);
    WriteLittle(result, 80, 0x10000, 8);
    WriteLittle(result, 96, result.size() - kPayload, 8);
    WriteLittle(result, 104, result.size() - kPayload, 8);
    WriteLittle(result, 112, kPayload, 8);
    for (std::size_t i = 0; i < kWords.size(); ++i)
        WriteLittle(result, kPayload + 4*i, kWords[i], 4);
    return result;
}

bool InstallSvc(GuestMemory& memory) {
    return memory.Map(0x1000, kPage, Access::Read | Access::Write) &&
           memory.Write32(0x1000, 0xd4000001) &&
           memory.Protect(0x1000, kPage, Access::Read | Access::Execute) &&
           memory.Map(0x2000, kPage, Access::Read | Access::Write);
}

SessionResult Call(SyntheticSession& guest, std::uint64_t service,
                   std::uint64_t x0 = 0, std::uint64_t x1 = 0,
                   std::uint64_t x2 = 0) {
    auto& regs = guest.Context();
    regs.pc = 0x1000;
    regs.x[8] = service;
    regs.x[0] = x0; regs.x[1] = x1; regs.x[2] = x2;
    return guest.Run(1);
}
std::uint64_t U64(const GuestMemory& memory, std::uint64_t address) {
    return memory.Read64(address).value_or(UINT64_MAX);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        const auto image = GuestElf();
        std::ofstream file(argv[2], std::ios::binary | std::ios::trunc);
        return file.write(reinterpret_cast<const char*>(image.data()),
                          static_cast<std::streamsize>(image.size())) ?
               EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using namespace nint2;

    // Original ELF executes real guest SVCs, stores bytes in its own newly
    // mapped pages, prints them, changes W^X, queries, and frees the region.
    GuestMemory program_memory(0x40000000);
    const auto elf = GuestElf();
    const auto loaded = LoadElf64(elf, program_memory);
    REQUIRE(loaded.error == ElfError::None && loaded.entry == 0x10000);
    REQUIRE(program_memory.Map(0x3000, kPage, Access::Read | Access::Write));
    SyntheticSession program(program_memory, loaded.entry, 0x4000);
    program.Context().x[20] = 0x3000;
    const auto outcome = program.Run(128);
    REQUIRE(outcome.stop == SessionStop::Exited && outcome.exit_code == 0);
    REQUIRE(program.Stdout() == "VM!\n" && program.Stderr().empty());
    REQUIRE(U64(program_memory, 0x3000) == kArenaBase);
    REQUIRE(U64(program_memory, 0x3008) == kPage);
    REQUIRE(U64(program_memory, 0x3010) == 5); // RX permissions, no writable alias
    REQUIRE(program.ArenaAllocatedBytes() == 0);
    REQUIRE(program.ArenaRegionCount() == 0);
    REQUIRE(!program_memory.Read8(kArenaBase).has_value());
    REQUIRE(!program_memory.Fetch32(kArenaBase).has_value());
    REQUIRE(program.Run(2).stop == SessionStop::Exited);
    REQUIRE(program.TotalSteps() == outcome.steps);

    // Short VA spaces cannot accommodate the arena. The original guest
    // observes a negative allocation status and takes its defined error path.
    GuestMemory unavailable_memory(0x40000);
    REQUIRE(LoadElf64(elf, unavailable_memory).error == ElfError::None);
    REQUIRE(unavailable_memory.Map(0x3000, kPage, Access::Read | Access::Write));
    SyntheticSession unavailable(unavailable_memory, 0x10000, 0x4000);
    unavailable.Context().x[20] = 0x3000;
    const auto unavailable_result = unavailable.Run(128);
    REQUIRE(unavailable_result.stop == SessionStop::Exited);
    REQUIRE(unavailable_result.exit_code == 77);
    REQUIRE(unavailable.Stdout().empty() && unavailable.ArenaRegionCount() == 0);

    // Test malformed SVC operands, W^X, external aliases, and transactional
    // queries through actual AArch64 traps and checked GuestMemory pages.
    GuestMemory memory(0x40000000);
    REQUIRE(InstallSvc(memory));
    SyntheticSession guest(memory, 0x1000, 0x3000);
    REQUIRE(Call(guest, 18, 0, 3).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 18, 4095, 3).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 18, kPage, 7).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 18, kPage, 0x100).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 18, kPage, 3, kArenaBase + 1).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(guest.ArenaAllocatedBytes() == 0 && guest.ArenaRegionCount() == 0);
    REQUIRE(Call(guest, 18, kPage, 3).stop == SessionStop::StepLimit);
    const auto first = guest.Context().x[0];
    REQUIRE(first == kArenaBase);
    REQUIRE(guest.ArenaAllocatedBytes() == kPage);
    REQUIRE(memory.Read64(first) == 0); // freshly committed lazy-zero page
    REQUIRE(memory.Write32(first, 0xd503201f)); // original A64 NOP
    REQUIRE(!memory.Fetch32(first).has_value()); // RW is non-executable

    REQUIRE(Call(guest, 18, kPage, 3, first).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-17LL));
    REQUIRE(Call(guest, 21, first + 17, 0x2100).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(U64(memory, 0x2100) == first && U64(memory, 0x2108) == kPage);
    REQUIRE(U64(memory, 0x2110) == 3);

    // Failed writes across a page boundary must leave all earlier bytes
    // unchanged, including the region's current page metadata.
    for (std::uint64_t i = 0; i < 8; ++i)
        REQUIRE(memory.Write8(0x2ff8 + i, 0x65));
    REQUIRE(Call(guest, 21, first, 0x2ff8).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    for (std::uint64_t i = 0; i < 8; ++i)
        REQUIRE(memory.Read8(0x2ff8 + i) == 0x65);

    // A writable alias elsewhere in the same GuestMemory prevents making
    // the original page executable, preserving W^X across all virtual names.
    REQUIRE(memory.Alias(0x30000000, first, kPage, Access::Read | Access::Write));
    REQUIRE(Call(guest, 20, first, 5).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-12LL));
    REQUIRE(memory.Write8(0x30000000, 0xab));
    REQUIRE(memory.Read8(first) == 0xab);
    REQUIRE(memory.Unmap(0x30000000, kPage));
    REQUIRE(memory.Write32(first, 0xd503201f));
    REQUIRE(Call(guest, 20, first, 5).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(memory.Fetch32(first) == 0xd503201f);
    REQUIRE(!memory.Write32(first, 0xd4200000));
    REQUIRE(Call(guest, 21, first, 0x2100).stop == SessionStop::StepLimit);
    REQUIRE(U64(memory, 0x2110) == 5);
    REQUIRE(Call(guest, 20, first, 6).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));

    // Impossible pointers and unknown regions do not access arbitrary host
    // bytes. Freeing an exact allocation returns its pages to the arena.
    REQUIRE(Call(guest, 21, UINT64_MAX, 0x2100).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 19, first + kPage).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 19, first).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(guest.ArenaAllocatedBytes() == 0);
    REQUIRE(Call(guest, 19, first).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Call(guest, 18, kPage, 3).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == first);
    REQUIRE(memory.Read32(first) == 0); // no stale page data survives a free
    REQUIRE(Call(guest, 19, first).stop == SessionStop::StepLimit);

    // Process-isolated memory: identical virtual addresses have unrelated
    // physical page frames, write ownership and release lifecycles.
    GuestMemory private_memory(0x40000000);
    REQUIRE(InstallSvc(private_memory));
    SyntheticSession peer(private_memory, 0x1000, 0x3000);
    REQUIRE(Call(guest, 18, kPage, 3).stop == SessionStop::StepLimit);
    REQUIRE(Call(peer, 18, kPage, 3).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == first && peer.Context().x[0] == first);
    REQUIRE(memory.Write64(first, 0x1122334455667788ULL));
    REQUIRE(private_memory.Read64(first) == 0);
    REQUIRE(private_memory.Write64(first, 0xaabbccddeeff0011ULL));
    REQUIRE(memory.Read64(first) == 0x1122334455667788ULL);
    REQUIRE(Call(peer, 19, first).stop == SessionStop::StepLimit);
    REQUIRE(memory.Read64(first) == 0x1122334455667788ULL);
    REQUIRE(Call(guest, 19, first).stop == SessionStop::StepLimit);

    // Terminal guest CPU faults must also relinquish guest heap pages when
    // the scheduler retires that process; repeat retirement is harmless.
    GuestMemory dying_memory(0x40000000);
    REQUIRE(InstallSvc(dying_memory)); // uninitialized zero instruction at +4
    SyntheticSession dying(dying_memory, 0x1000, 0x3000);
    REQUIRE(Call(dying, 18, kPage, 3).stop == SessionStop::StepLimit);
    REQUIRE(dying.ArenaAllocatedBytes() == kPage);
    REQUIRE(dying_memory.Read64(kArenaBase) == 0);
    SyntheticScheduler dying_scheduler;
    REQUIRE(dying_scheduler.Add(dying));
    const auto failed = dying_scheduler.Run(8);
    REQUIRE(failed.stop == SchedulerStop::GuestFailed && failed.failed == 1);
    REQUIRE(dying.ArenaRegionCount() == 0);
    REQUIRE(dying.ArenaAllocatedBytes() == 0);
    REQUIRE(!dying_memory.Read8(kArenaBase).has_value());
    dying.RetireHandles();
    REQUIRE(dying.ArenaRegionCount() == 0);

    // Unknown SVCs still surface an exception and leave the instruction PC
    // in place instead of silently succeeding.
    REQUIRE(Call(guest, 99).stop == SessionStop::UnsupportedSupervisorCall);
    REQUIRE(guest.Context().pc == 0x1000);

    std::cout << "Independent ARM64 ELF: arena mapping, W^X, queries, "
                 "isolated memory and clean teardown passed\n";
    return EXIT_SUCCESS;
}
