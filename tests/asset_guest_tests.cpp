#include "nint2/assets.hpp"
#include "nint2/elf.hpp"
#include "nint2/session.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#define REQUIRE(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while(false)

namespace {
using namespace nint2;
constexpr std::string_view kPath = "/demo/message";
constexpr std::string_view kText = "Loaded from asset!\n";

// Produced by Clang's AArch64 assembler from our source-controlled
// tests/fixtures/original_asset_guest.s; not copied from a console binary.
constexpr std::array<std::uint32_t, 34> kWords{
    0xd2800148, 0x10000420, 0xd28001a1, 0xd2800002, 0xd4000001,
    0xf100001f, 0x5400032b, 0xaa0003f3, 0xd2800168, 0xaa1303e0,
    0xaa1403e1, 0xd2800262, 0xd4000001, 0xf1004c1f, 0x54000221,
    0xaa0003f5, 0xd2800028, 0xd2800020, 0xaa1403e1, 0xaa1503e2,
    0xd4000001, 0xf1004c1f, 0x54000121, 0xd28001a8, 0xaa1303e0,
    0xd4000001, 0xf100001f, 0x54000081, 0xd2800048, 0xd2800000,
    0xd4000001, 0xd2800048, 0xd2800b40, 0xd4000001
};

void Little(std::vector<std::uint8_t>& buffer, std::size_t address,
            std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        buffer[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> OriginalAssetElf() {
    constexpr std::size_t kPayload = 0x100;
    std::vector<std::uint8_t> image(kPayload + kWords.size() * 4 + kPath.size(), 0);
    image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
    image[4] = 2; image[5] = 1; image[6] = 1;
    Little(image, 16, 2, 2);     // ET_EXEC
    Little(image, 18, 183, 2);   // EM_AARCH64
    Little(image, 20, 1, 4);
    Little(image, 24, 0x10000, 8);
    Little(image, 32, 64, 8);
    Little(image, 52, 64, 2);
    Little(image, 54, 56, 2);
    Little(image, 56, 1, 2);
    Little(image, 64, 1, 4);     // PT_LOAD
    Little(image, 68, 5, 4);     // PF_R|PF_X
    Little(image, 72, kPayload, 8);
    Little(image, 80, 0x10000, 8);
    Little(image, 96, image.size() - kPayload, 8);
    Little(image, 104, image.size() - kPayload, 8);
    Little(image, 112, 0x100, 8);
    for (std::size_t i = 0; i < kWords.size(); ++i)
        Little(image, kPayload + i * 4, kWords[i], 4);
    for (std::size_t i = 0; i < kPath.size(); ++i)
        image[kPayload + kWords.size() * 4 + i] = static_cast<std::uint8_t>(kPath[i]);
    return image;
}

std::vector<std::uint8_t> Payload(std::string_view text) {
    return {text.begin(), text.end()};
}

bool InstallSvc(GuestMemory& memory) {
    return memory.Map(0x1000, 4096, Access::Read | Access::Write) &&
           memory.Write32(0x1000, 0xd4000001) &&
           memory.Protect(0x1000, 4096, Access::Read | Access::Execute) &&
           memory.Map(0x2000, 4096, Access::Read | Access::Write);
}

bool WriteText(GuestMemory& memory, std::uint64_t start, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (!memory.Write8(start + i, static_cast<std::uint8_t>(text[i]))) return false;
    return true;
}

SessionResult Invoke(SyntheticSession& guest, std::uint64_t service,
                     std::uint64_t x0 = 0, std::uint64_t x1 = 0,
                     std::uint64_t x2 = 0, std::uint64_t x3 = 0) {
    auto& cpu = guest.Context();
    cpu.pc = 0x1000;
    cpu.x[8] = service;
    cpu.x[0] = x0; cpu.x[1] = x1; cpu.x[2] = x2; cpu.x[3] = x3;
    return guest.Run(1);
}

std::uint64_t Word(const GuestMemory& memory, std::uint64_t address) {
    return memory.Read64(address).value_or(UINT64_MAX);
}

bool BytesAre(const GuestMemory& memory, std::uint64_t address,
              std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (memory.Read8(address + i) != static_cast<std::uint8_t>(text[i])) return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        const auto image = OriginalAssetElf();
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        return output.write(reinterpret_cast<const char*>(image.data()),
                            static_cast<std::streamsize>(image.size())) ?
               EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using nint2::assets::Error;

    // Original source -> self-contained ELF -> actual interpreter and SVCs.
    const auto elf = OriginalAssetElf();
    GuestMemory elf_memory(0x40000);
    const auto load = LoadElf64(elf, elf_memory);
    REQUIRE(load.error == ElfError::None && load.entry == 0x10000);
    REQUIRE(elf_memory.Map(0x3000, 4096, Access::Read | Access::Write));
    SyntheticSession original(elf_memory, load.entry, 0x4000);
    auto input = Payload(kText);
    REQUIRE(original.MountAsset(kPath, input).Ok());
    REQUIRE(original.AssetCount() == 1 && original.OpenAssetCount() == 0);
    original.Context().x[20] = 0x3000;
    // Mount copied its host-owned buffer, not a pointer to host storage.
    input[0] = '?';
    const auto completed = original.Run(150);
    REQUIRE(completed.stop == SessionStop::Exited && completed.exit_code == 0);
    REQUIRE(completed.steps > 20 && completed.steps < 100);
    REQUIRE(original.Stdout() == kText && original.Stderr().empty());
    REQUIRE(original.OpenAssetCount() == 0);
    REQUIRE(original.Context().pc == 0x10078);
    REQUIRE(BytesAre(elf_memory, 0x3000, kText));
    REQUIRE(original.MountAsset("/late", input).error == Error::Sealed);
    REQUIRE(original.Run(1).stop == SessionStop::Exited);
    REQUIRE(original.TotalSteps() == completed.steps);

    // Missing assets and truncated content must take the guest's error path;
    // the host must not fake successful output or silently create files.
    GuestMemory missing_mem(0x40000);
    REQUIRE(LoadElf64(elf, missing_mem).error == ElfError::None);
    REQUIRE(missing_mem.Map(0x3000, 4096, Access::Read | Access::Write));
    SyntheticSession missing(missing_mem, 0x10000, 0x4000);
    missing.Context().x[20] = 0x3000;
    const auto not_found = missing.Run(150);
    REQUIRE(not_found.stop == SessionStop::Exited && not_found.exit_code == 90);
    REQUIRE(missing.Stdout().empty() && missing.OpenAssetCount() == 0);

    GuestMemory short_mem(0x40000);
    REQUIRE(LoadElf64(elf, short_mem).error == ElfError::None);
    REQUIRE(short_mem.Map(0x3000, 4096, Access::Read | Access::Write));
    SyntheticSession shorter(short_mem, 0x10000, 0x4000);
    REQUIRE(shorter.MountAsset(kPath, Payload("short")).Ok());
    shorter.Context().x[20] = 0x3000;
    REQUIRE(shorter.Run(150).exit_code == 90);
    REQUIRE(shorter.Stdout().empty() && shorter.OpenAssetCount() == 0);

    // Direct SVC tests check descriptor offsets, seek, read-at, metadata,
    // enumeration, all-or-nothing cross-page writes and malformed pointers.
    GuestMemory memory(0x10000);
    REQUIRE(InstallSvc(memory));
    REQUIRE(WriteText(memory, 0x2100, kPath));
    SyntheticSession guest(memory, 0x1000, 0x4000);
    const auto bytes = Payload(kText);
    REQUIRE(guest.MountAsset(kPath, bytes).Ok());
    REQUIRE(guest.MountAsset("/zzz/empty", {}).Ok());
    REQUIRE(guest.AssetCount() == 2);
    REQUIRE(Invoke(guest, 10, 0x2100, kPath.size(), 1).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Invoke(guest, 10, 0x2ffe, kPath.size(), 0).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    REQUIRE(Invoke(guest, 10, 0x2100, kPath.size(), 0).stop == SessionStop::StepLimit);
    const auto fd = guest.Context().x[0];
    REQUIRE(fd >= assets::AssetStore::kFirstHandle && fd <= UINT32_MAX);
    REQUIRE(guest.OpenAssetCount() == 1);
    REQUIRE(guest.MountAsset("/late", bytes).error == Error::Sealed);

    // Preserve existing guest bytes and the descriptor cursor on EFAULT.
    for (std::uint64_t i = 0; i < 5; ++i)
        REQUIRE(memory.Write8(0x2ffb + i, 0x7f));
    REQUIRE(Invoke(guest, 11, fd, 0x2ffb, kText.size()).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    for (std::uint64_t i = 0; i < 5; ++i)
        REQUIRE(memory.Read8(0x2ffb + i) == 0x7f);
    REQUIRE(memory.Map(0x3000, 4096, Access::Read));
    REQUIRE(Invoke(guest, 11, fd, 0x2ffb, kText.size()).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    REQUIRE(memory.Protect(0x3000, 4096, Access::Read | Access::Write));
    REQUIRE(Invoke(guest, 11, fd, 0x2ffb, kText.size()).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == kText.size());
    REQUIRE(BytesAre(memory, 0x2ffb, kText));

    // Atomic metadata updates: size, FNV digest, cursor in little-endian form.
    REQUIRE(Invoke(guest, 17, fd, 0x3100).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(Word(memory, 0x3100) == kText.size());
    REQUIRE(Word(memory, 0x3108) == assets::AssetStore::Hash(bytes));
    REQUIRE(Word(memory, 0x3110) == kText.size());
    REQUIRE(Invoke(guest, 11, fd, UINT64_MAX, 4096).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0); // EOF does not touch an invalid pointer

    // Signed seek from the end, then positioned read without cursor movement.
    const auto backwards = std::bit_cast<std::uint64_t>(std::int64_t{-5});
    REQUIRE(Invoke(guest, 12, fd, backwards, 2).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == kText.size() - 5);
    REQUIRE(Invoke(guest, 16, fd, 0x3300, 5, 0).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 5);
    REQUIRE(BytesAre(memory, 0x3300, "Loade"));
    REQUIRE(Invoke(guest, 17, fd, 0x3120).stop == SessionStop::StepLimit);
    REQUIRE(Word(memory, 0x3130) == kText.size() - 5);
    REQUIRE(Invoke(guest, 12, fd, UINT64_MAX, 0).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Invoke(guest, 12, fd, 0, 4).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(Invoke(guest, 11, fd, 0x3300, 4097).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-22LL));

    // Path metadata and sorted enumeration use bounded transactional writes.
    REQUIRE(Invoke(guest, 14, 0x2100, kPath.size(), 0x31f8).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(Word(memory, 0x31f8) == kText.size());
    REQUIRE(Word(memory, 0x3200) == assets::AssetStore::Hash(bytes));
    REQUIRE(Invoke(guest, 14, 0x2100, kPath.size(), 0x3ffc).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-14LL));
    REQUIRE(Invoke(guest, 15, 0, 0x3400, 151).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-90LL));
    REQUIRE(Invoke(guest, 15, 0, 0x3400, 152).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 1);
    REQUIRE(Word(memory, 0x3400) == kText.size());
    REQUIRE(Word(memory, 0x3408) == assets::AssetStore::Hash(bytes));
    REQUIRE(Word(memory, 0x3410) == kPath.size());
    REQUIRE(BytesAre(memory, 0x3418, kPath));
    REQUIRE(Invoke(guest, 15, 1, 0x3500, 152).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 1);
    REQUIRE(Word(memory, 0x3500) == 0);
    REQUIRE(Invoke(guest, 15, 2, UINT64_MAX, 0).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0); // enumeration EOF: no guest write

    // Independent opens have independent offsets; close never recycles ids.
    REQUIRE(Invoke(guest, 10, 0x2100, kPath.size(), 0).stop == SessionStop::StepLimit);
    const auto fd2 = guest.Context().x[0];
    REQUIRE(fd2 > fd);
    REQUIRE(Invoke(guest, 17, fd2, 0x3600).stop == SessionStop::StepLimit);
    REQUIRE(Word(memory, 0x3610) == 0);
    REQUIRE(Invoke(guest, 13, fd).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == 0);
    REQUIRE(Invoke(guest, 13, fd).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-9LL));
    REQUIRE(Invoke(guest, 11, fd, 0x3300, 5).stop == SessionStop::StepLimit);
    REQUIRE(guest.Context().x[0] == static_cast<std::uint64_t>(-9LL));
    REQUIRE(guest.OpenAssetCount() == 1);
    REQUIRE(Invoke(guest, 13, fd2).stop == SessionStop::StepLimit);
    REQUIRE(guest.OpenAssetCount() == 0);

    // Same guest virtual address and pathname do not reveal another
    // session's host-mounted assets; each session has a private manifest.
    GuestMemory private_memory(0x10000);
    REQUIRE(InstallSvc(private_memory));
    REQUIRE(WriteText(private_memory, 0x2100, kPath));
    SyntheticSession private_guest(private_memory, 0x1000, 0x4000);
    REQUIRE(Invoke(private_guest, 10, 0x2100, kPath.size(), 0).stop == SessionStop::StepLimit);
    REQUIRE(private_guest.Context().x[0] == static_cast<std::uint64_t>(-2LL));
    REQUIRE(private_guest.AssetCount() == 0);
    REQUIRE(private_guest.Stdout().empty());

    std::cout << "Original AArch64 ELF asset loading, integrity, SVC isolation and faults passed\n";
    return EXIT_SUCCESS;
}
