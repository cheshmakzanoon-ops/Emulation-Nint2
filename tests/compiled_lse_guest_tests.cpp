#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/session.hpp"

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "compiled LSE guest failure: " #expr " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
// Clang 17/LLD original_lse_guest.c -O1 -march=armv8.1-a+lse
// -mgeneral-regs-only --target=aarch64-none-elf. The independently authored
// source is tracked separately; original machine words are recorded here so
// default Windows/Linux CTest never requires a cross compiler to execute it.
// All guest virtual addresses and code/data constants below reproduce the
// independently generated executable's three relevant PT_LOAD segments.
constexpr std::array<std::uint32_t, 69> kProgram{
    0x528000e9, 0xd503201f, 0x100109c8, 0xf8e90109, 0xf1000d3f, 0x54000501,
    0x52800549, 0x5280014a, 0xc8eafd09, 0xf100295f, 0x54000521, 0x52800c6a,
    0x52800529, 0xc8e9fd0a, 0xf100a53f, 0x54000540, 0xf100a93f, 0x54000501,
    0x32089fea, 0xd0000009, 0x91052129, 0xb8ea212a, 0x32009feb, 0x6b0b015f,
    0x540004e1, 0x88dffd29, 0x3100053f, 0x54000481, 0xc8dffd08, 0xf100a91f,
    0x54000421, 0x52800020, 0x52800028, 0xd503201f, 0x10008461, 0x528004a2,
    0xd4000001, 0xf100941f, 0x52800be8, 0xaa1f03e1, 0x9a8803e0, 0xaa1f03e2,
    0x52800048, 0xd4000001, 0xd65f03c0, 0x52800b60, 0xaa1f03e1, 0xaa1f03e2,
    0x52800048, 0xd4000001, 0xd65f03c0, 0x52800b80, 0xaa1f03e1, 0xaa1f03e2,
    0x52800048, 0xd4000001, 0xd65f03c0, 0x52800ba0, 0xaa1f03e1, 0xaa1f03e2,
    0x52800048, 0xd4000001, 0xd65f03c0, 0x52800bc0, 0xaa1f03e1, 0xaa1f03e2,
    0x52800048, 0xd4000001, 0xd65f03c0
};
constexpr std::string_view kMessage = "Compiled ARM64 LSE atomics verified!\n";
constexpr std::uint64_t kCode = 0x10000;
constexpr std::uint64_t kReadOnly = 0x11114;
constexpr std::uint64_t kWritable = 0x12140;
constexpr std::size_t kCodeFileOffset = 0x1000;
constexpr std::size_t kReadOnlyFileOffset = 0x1114;
constexpr std::size_t kWritableFileOffset = 0x1140;
static_assert(kProgram.size() * 4 == 0x114);
static_assert(kMessage.size() == 37);

void Put(std::vector<std::uint8_t>& bytes, std::size_t offset,
         std::uint64_t word, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(word >> (8 * i));
}
void ProgramHeader(std::vector<std::uint8_t>& bytes, unsigned index,
                   std::uint64_t flags, std::uint64_t offset,
                   std::uint64_t virtual_address, std::uint64_t size) {
    const auto position = 64 + 56 * index;
    Put(bytes, position, 1, 4);       // PT_LOAD
    Put(bytes, position + 4, flags, 4);
    Put(bytes, position + 8, offset, 8);
    Put(bytes, position + 16, virtual_address, 8);
    Put(bytes, position + 32, size, 8); // p_filesz
    Put(bytes, position + 40, size, 8); // p_memsz
    Put(bytes, position + 48, 0x1000, 8);
}
std::vector<std::uint8_t> Image() {
    std::vector<std::uint8_t> bytes(kWritableFileOffset + 12);
    bytes[0] = 0x7f;
    bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    Put(bytes, 16, 2, 2);     // ET_EXEC
    Put(bytes, 18, 183, 2);   // EM_AARCH64
    Put(bytes, 20, 1, 4);     // ELF version
    Put(bytes, 24, kCode, 8); // entry
    Put(bytes, 32, 64, 8);    // program headers
    Put(bytes, 52, 64, 2);    // ELF header bytes
    Put(bytes, 54, 56, 2);    // ELF64 program-header bytes
    Put(bytes, 56, 3, 2);     // three separate PT_LOAD segments
    ProgramHeader(bytes, 0, 5, kCodeFileOffset, kCode, 0x114); // RX
    ProgramHeader(bytes, 1, 4, kReadOnlyFileOffset, kReadOnly, 0x26); // R
    ProgramHeader(bytes, 2, 6, kWritableFileOffset, kWritable, 12); // RW
    for (unsigned i = 0; i < kProgram.size(); ++i)
        Put(bytes, kCodeFileOffset + 4 * i, kProgram[i], 4);
    for (std::size_t i = 0; i < kMessage.size(); ++i)
        bytes[kReadOnlyFileOffset + i] = static_cast<std::uint8_t>(kMessage[i]);
    bytes[kReadOnlyFileOffset + kMessage.size()] = 0;
    Put(bytes, kWritableFileOffset, 3, 8); // source C _Atomic uint64_t counter
    Put(bytes, kWritableFileOffset + 8, 0x00ff00ff, 4); // _Atomic uint32_t mask
    return bytes;
}
struct Execution {
    nint2::SessionResult result{};
    std::string output;
    std::uint64_t counter{};
    std::uint32_t mask{};
};
Execution Execute(const std::vector<std::uint8_t>& bytes,
                  std::uint32_t override_counter = 3,
                  std::uint32_t override_mask = 0x00ff00ff) {
    nint2::GuestMemory memory(0x400000);
    const auto loaded = nint2::LoadElf64(bytes, memory);
    if (loaded.error != nint2::ElfError::None || loaded.entry != kCode) {
        std::cerr << "original LSE test ELF loader rejected code\n";
        std::exit(EXIT_FAILURE);
    }
    if (memory.Fetch32(kCode) != kProgram[0] ||
        memory.Fetch32(kReadOnly) || memory.Fetch32(kWritable) ||
        memory.Write8(kCode, 0) || memory.Write8(kReadOnly, 0)) {
        std::cerr << "LSE test ELF guest segment permissions wrong\n";
        std::exit(EXIT_FAILURE);
    }
    if (!memory.Write64(kWritable, override_counter) ||
        !memory.Write32(kWritable + 8, override_mask) ||
        !memory.Map(0x20000, 4096, nint2::Access::Read | nint2::Access::Write)) {
        std::cerr << "LSE guest stack/data setup failed\n";
        std::exit(EXIT_FAILURE);
    }
    nint2::SyntheticSession guest(memory, loaded.entry, 0x21000);
    const auto finished = guest.Run(2000);
    const auto replay = guest.Run(100);
    if (replay.stop != finished.stop || replay.steps != finished.steps) {
        std::cerr << "LSE guest state not terminal after Exit\n";
        std::exit(EXIT_FAILURE);
    }
    return {finished, std::string(guest.Stdout()),
            memory.Read64(kWritable).value_or(UINT64_MAX),
            memory.Read32(kWritable + 8).value_or(UINT32_MAX)};
}
} // namespace

int main(int argc, char** argv) {
    const auto image = Image();
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        return output.write(reinterpret_cast<const char*>(image.data()),
                            static_cast<std::streamsize>(image.size()))
                   ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using nint2::SessionStop;
    const auto good = Execute(image);
    CHECK(good.result.stop == SessionStop::Exited);
    CHECK(good.result.exit_code == 0);
    CHECK(good.result.steps > 20 && good.result.steps < 300);
    CHECK(good.output == kMessage);
    CHECK(good.counter == 42 && good.mask == UINT32_MAX);

    // Tampering guest initialization exercises both the original compiler's
    // early-exit error path and the full pipeline without fake host results.
    const auto wrong_counter = Execute(image, 0);
    CHECK(wrong_counter.result.stop == SessionStop::Exited);
    CHECK(wrong_counter.result.exit_code == 91);
    CHECK(wrong_counter.output.empty());
    CHECK(wrong_counter.counter == 7);

    const auto wrong_mask = Execute(image, 3, 0);
    CHECK(wrong_mask.result.stop == SessionStop::Exited);
    CHECK(wrong_mask.result.exit_code == 94);
    CHECK(wrong_mask.output.empty());
    CHECK(wrong_mask.counter == 42);

    // Loader-derived constant/code pages stay nonwritable and executable/data
    // pages stay disjoint. Repeat from fresh state for deterministic replay.
    const auto second = Execute(image);
    CHECK(second.result.steps == good.result.steps);
    CHECK(second.output == good.output);
    CHECK(second.counter == good.counter && second.mask == good.mask);

    auto bad = image;
    bad[0] = 0;
    nint2::GuestMemory invalid(0x400000);
    CHECK(nint2::LoadElf64(bad, invalid).error != nint2::ElfError::None);
    CHECK(!invalid.Fetch32(kCode));
    std::cout << "Freestanding Clang-compiled AArch64 LSE guest:"
                 " four end-to-end runs, atomic output, ELF W^X and tamper paths passed\n";
    return EXIT_SUCCESS;
}
