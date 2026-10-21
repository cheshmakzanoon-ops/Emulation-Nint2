#include "nint2/arena.hpp"
#include "nint2/assets.hpp"
#include "nint2/elf.hpp"
#include "nint2/session.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAILED " << #expr << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using namespace nint2;
constexpr std::string_view kPath = "/demo/message";
constexpr std::string_view kText = "Loaded from asset!\n";
constexpr Access kRw = Access::Read | Access::Write;
constexpr Access kRx = Access::Read | Access::Execute;
constexpr std::uint64_t kPage = GuestMemory::kPageSize;

// These original machine words are independently reproduced by assembling
// tests/fixtures/original_heap_asset.s, verified byte-for-byte in Linux CI.
constexpr std::array<std::uint32_t, 46> kWords{
    0xd2800248, 0xd2820000, 0xd2800061, 0xd2800002, 0xd4000001, 0xf100001f,
    0x540004ab, 0xaa0003f3, 0xd2800148, 0x100004a0, 0xd28001a1, 0xd2800002,
    0xd4000001, 0xf100001f, 0x540003ab, 0xaa0003f5, 0xd2800168, 0xaa1503e0,
    0xaa1303e1, 0xd2800262, 0xd4000001, 0xf1004c1f, 0x540002a1, 0xd2800028,
    0xd2800020, 0xaa1303e1, 0xd2800262, 0xd4000001, 0xf1004c1f, 0x540001c1,
    0xd28001a8, 0xaa1503e0, 0xd4000001, 0xf100001f, 0x54000121, 0xd2800268,
    0xaa1303e0, 0xd4000001, 0xf100001f, 0x54000081, 0xd2800048, 0xd2800000,
    0xd4000001, 0xd2800048, 0xd2800b60, 0xd4000001
};

void Put(std::vector<std::uint8_t>& image, std::size_t offset,
         std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        image[offset + i] = static_cast<std::uint8_t>(value >> (8*i));
}

std::vector<std::uint8_t> AuthoredHeapElf() {
    constexpr std::size_t kOffset = 0x100;
    std::vector<std::uint8_t> image(kOffset + kWords.size()*4 + kPath.size());
    image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
    image[4] = 2; image[5] = 1; image[6] = 1;
    Put(image, 16, 2, 2); Put(image, 18, 183, 2);
    Put(image, 20, 1, 4); Put(image, 24, 0x10000, 8);
    Put(image, 32, 64, 8); Put(image, 52, 64, 2);
    Put(image, 54, 56, 2); Put(image, 56, 1, 2);
    Put(image, 64, 1, 4); Put(image, 68, 5, 4);
    Put(image, 72, kOffset, 8); Put(image, 80, 0x10000, 8);
    Put(image, 96, image.size() - kOffset, 8);
    Put(image, 104, image.size() - kOffset, 8);
    Put(image, 112, 0x100, 8);
    for (std::size_t i = 0; i < kWords.size(); ++i)
        Put(image, kOffset + 4*i, kWords[i], 4);
    for (std::size_t i = 0; i < kPath.size(); ++i)
        image[kOffset + 4*kWords.size() + i] = static_cast<std::uint8_t>(kPath[i]);
    return image;
}

std::vector<std::uint8_t> Bytes(std::string_view value) {
    return {value.begin(), value.end()};
}

bool BuildSvcMemory(GuestMemory& memory) {
    return memory.Map(0x1000, kPage, kRw) &&
           memory.Write32(0x1000, 0xd4000001) &&
           memory.Protect(0x1000, kPage, kRx) &&
           memory.Map(0x2000, kPage, kRw);
}

SessionResult Invoke(SyntheticSession& guest, std::uint64_t service,
                     std::uint64_t arg0 = 0, std::uint64_t arg1 = 0,
                     std::uint64_t arg2 = 0, std::uint64_t arg3 = 0) {
    auto& context = guest.Context();
    context.pc = 0x1000;
    context.x[8] = service;
    context.x[0] = arg0; context.x[1] = arg1;
    context.x[2] = arg2; context.x[3] = arg3;
    return guest.Run(1);
}

std::uint64_t Word(const GuestMemory& memory, std::uint64_t address) {
    return memory.Read64(address).value_or(UINT64_MAX);
}

bool IsText(const GuestMemory& memory, std::uint64_t address, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (memory.Read8(address + i) != static_cast<std::uint8_t>(text[i]))
            return false;
    return true;
}

bool WriteText(GuestMemory& memory, std::uint64_t address, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (!memory.Write8(address + i, static_cast<std::uint8_t>(text[i])))
            return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        const auto elf = AuthoredHeapElf();
        std::ofstream file(argv[2], std::ios::binary | std::ios::trunc);
        return file.write(reinterpret_cast<const char*>(elf.data()),
                          static_cast<std::streamsize>(elf.size())) ?
               EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;

    // Original ELF executes Allocate -> Open -> Read -> Write -> Close ->
    // Free -> Exit, using its own dynamically allocated guest buffer.
    const auto elf = AuthoredHeapElf();
    GuestMemory executable(0x40000000);
    const auto loaded = LoadElf64(elf, executable);
    CHECK(loaded.error == ElfError::None && loaded.entry == 0x10000);
    SyntheticSession app(executable, loaded.entry, 0x4000);
    CHECK(app.MountAsset(kPath, Bytes(kText)).Ok());
    CHECK(app.ArenaRegionCount() == 0);
    const auto success = app.Run(200);
    CHECK(success.stop == SessionStop::Exited);
    CHECK(success.exit_code == 0);
    CHECK(success.steps >= 38 && success.steps <= 46);
    CHECK(app.Stdout() == kText);
    CHECK(app.Stderr().empty());
    CHECK(app.OpenAssetCount() == 0);
    CHECK(app.ArenaRegionCount() == 0);
    CHECK(app.ArenaAllocatedBytes() == 0);
    CHECK(!executable.Read8(vm::GuestArena::kBase).has_value());
    CHECK(app.Context().pc == 0x100a8);
    CHECK(app.Run(100).steps == success.steps);

    // Missing and short host-provisioned assets must not fabricate a success;
    // failed guests are nevertheless bounded and their descriptors retire.
    GuestMemory absent_space(0x40000000);
    CHECK(LoadElf64(elf, absent_space).error == ElfError::None);
    SyntheticSession absent(absent_space, 0x10000, 0x4000);
    const auto missing = absent.Run(100);
    CHECK(missing.stop == SessionStop::Exited && missing.exit_code == 91);
    CHECK(absent.Stdout().empty());
    CHECK(absent.OpenAssetCount() == 0);
    CHECK(absent.ArenaRegionCount() == 0); // Exit releases unfreed guest pages
    CHECK(absent.ArenaAllocatedBytes() == 0);
    CHECK(!absent_space.Read8(vm::GuestArena::kBase).has_value());

    GuestMemory short_space(0x40000000);
    CHECK(LoadElf64(elf, short_space).error == ElfError::None);
    SyntheticSession short_guest(short_space, 0x10000, 0x4000);
    CHECK(short_guest.MountAsset(kPath, Bytes("short")).Ok());
    const auto short_run = short_guest.Run(100);
    CHECK(short_run.stop == SessionStop::Exited && short_run.exit_code == 91);
    CHECK(short_guest.Stdout().empty());
    CHECK(short_guest.OpenAssetCount() == 0);
    CHECK(short_guest.ArenaRegionCount() == 0);
    CHECK(short_guest.ArenaAllocatedBytes() == 0);
    CHECK(!short_space.Read8(vm::GuestArena::kBase).has_value());

    // Direct synthetic SVC checks use executable memory with a single SVC #0.
    GuestMemory space(0x40000000);
    CHECK(BuildSvcMemory(space));
    SyntheticSession guest(space, 0x1000, 0x3000);
    CHECK(Invoke(guest, 18, 0, 3, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 18, 1, 3, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 18, kPage, 7, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 18, kPage, 256, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(guest.ArenaRegionCount() == 0);

    CHECK(Invoke(guest, 18, 2*kPage, 3, 0).stop == SessionStop::StepLimit);
    const auto base = guest.Context().x[0];
    CHECK(base == vm::GuestArena::kBase);
    CHECK(guest.ArenaRegionCount() == 1);
    CHECK(guest.ArenaAllocatedBytes() == 2*kPage);
    CHECK(space.Read64(base) == 0 && space.Read64(base + kPage) == 0);
    CHECK(space.Write32(base, 0xd503201f));
    CHECK(space.Write8(base + kPage, 0xa5));
    CHECK(Invoke(guest, 21, base + kPage + 1, 0x2100).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(Word(space, 0x2100) == base);
    CHECK(Word(space, 0x2108) == 2*kPage);
    CHECK(Word(space, 0x2110) == 3);

    // Metadata copy is all-or-nothing when a record straddles a missing page.
    CHECK(space.Write64(0x2ff8, 0x1122334455667788ULL));
    CHECK(Invoke(guest, 21, base, 0x2ff8).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    CHECK(Word(space, 0x2ff8) == 0x1122334455667788ULL);
    CHECK(space.Map(0x3000, kPage, Access::Read));
    CHECK(Invoke(guest, 21, base, 0x2ff8).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    CHECK(Word(space, 0x2ff8) == 0x1122334455667788ULL);
    CHECK(space.Protect(0x3000, kPage, kRw));
    CHECK(Invoke(guest, 21, base, 0x2ff8).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(Word(space, 0x2ff8) == base);
    CHECK(Word(space, 0x3000) == 2*kPage);
    CHECK(Word(space, 0x3008) == 3);

    CHECK(Invoke(guest, 20, base, 7).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(space.Write8(base, 99));
    CHECK(Invoke(guest, 20, base + kPage, 5).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 20, base, 5).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(!space.Write8(base, 33));
    CHECK(space.Fetch32(base) == 0xd503201f || space.Fetch32(base) == 0xd5032063);
    CHECK(Invoke(guest, 21, base, 0x2100).stop == SessionStop::StepLimit);
    CHECK(Word(space, 0x2110) == 5);
    CHECK(Invoke(guest, 20, base, 3).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(!space.Fetch32(base).has_value());

    // Non-owned map and stale pointer are never silently accepted.
    CHECK(Invoke(guest, 19, base + kPage).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 19, base).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(guest.ArenaRegionCount() == 0 && guest.ArenaAllocatedBytes() == 0);
    CHECK(!space.Read8(base));
    CHECK(Invoke(guest, 19, base).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 21, base, 0x2100).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    CHECK(Invoke(guest, 18, kPage, 3, base + 1).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));

    CHECK(Invoke(guest, 18, kPage, 3, 0x10008000).stop == SessionStop::StepLimit);
    const auto fixed = guest.Context().x[0];
    CHECK(fixed == 0x10008000);
    CHECK(Invoke(guest, 18, kPage, 3, fixed).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == static_cast<std::uint64_t>(-17LL));
    CHECK(Invoke(guest, 18, kPage, 3, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == vm::GuestArena::kBase);
    CHECK(guest.ArenaRegionCount() == 2);
    CHECK(Invoke(guest, 19, fixed).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(Invoke(guest, 19, vm::GuestArena::kBase).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);

    // Asset-read and virtual arena integration: RX guest buffers reject data
    // copies, which do not advance file offsets. RW enables the same read.
    CHECK(WriteText(space, 0x2200, kPath));
    CHECK(guest.MountAsset(kPath, Bytes(kText)).error ==
          assets::Error::Sealed); // first Run sealed asset manifest

    GuestMemory resources(0x40000000);
    CHECK(BuildSvcMemory(resources));
    CHECK(WriteText(resources, 0x2200, kPath));
    SyntheticSession integration(resources, 0x1000, 0x3000);
    CHECK(integration.MountAsset(kPath, Bytes(kText)).Ok());
    CHECK(Invoke(integration, 18, kPage, 3, 0).stop == SessionStop::StepLimit);
    const auto buffer = integration.Context().x[0];
    CHECK(buffer == vm::GuestArena::kBase);
    CHECK(Invoke(integration, 10, 0x2200, kPath.size(), 0).stop == SessionStop::StepLimit);
    const auto fd = integration.Context().x[0];
    CHECK(fd >= assets::AssetStore::kFirstHandle);
    CHECK(Invoke(integration, 20, buffer, 5).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == 0);
    CHECK(Invoke(integration, 11, fd, buffer, kText.size()).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    CHECK(Invoke(integration, 17, fd, 0x2300).stop == SessionStop::StepLimit);
    CHECK(Word(resources, 0x2310) == 0);
    CHECK(Invoke(integration, 20, buffer, 3).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == 0);
    CHECK(Invoke(integration, 11, fd, buffer, kText.size()).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == kText.size());
    CHECK(IsText(resources, buffer, kText));
    CHECK(Invoke(integration, 17, fd, 0x2320).stop == SessionStop::StepLimit);
    CHECK(Word(resources, 0x2330) == kText.size());
    CHECK(Invoke(integration, 19, buffer).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == 0);
    CHECK(!resources.Read8(buffer));
    CHECK(Invoke(integration, 13, fd).stop == SessionStop::StepLimit);
    CHECK(integration.Context().x[0] == 0);

    // Two original sessions allocate the same guest address but never share
    // page frames, registers, manifest, or mapping metadata.
    GuestMemory peer_space(0x40000000);
    CHECK(BuildSvcMemory(peer_space));
    SyntheticSession peer(peer_space, 0x1000, 0x3000);
    CHECK(Invoke(guest, 18, kPage, 3, 0).stop == SessionStop::StepLimit);
    CHECK(Invoke(peer, 18, kPage, 3, 0).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == peer.Context().x[0]);
    const auto common = guest.Context().x[0];
    CHECK(space.Write8(common, 'A'));
    CHECK(peer_space.Read8(common) == 0);
    CHECK(peer_space.Write8(common, 'B'));
    CHECK(space.Read8(common) == 'A');
    CHECK(Invoke(guest, 19, common).stop == SessionStop::StepLimit);
    CHECK(guest.Context().x[0] == 0);
    CHECK(peer_space.Read8(common) == 'B');
    CHECK(peer.ArenaRegionCount() == 1);

    std::cout << "Original A64 heap+assets end-to-end, SVC memory contracts and isolation passed\n";
    return EXIT_SUCCESS;
}
