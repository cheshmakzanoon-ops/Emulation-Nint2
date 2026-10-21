#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/session.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
void Put(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint64_t x, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
}

std::vector<std::uint8_t> OriginalCompiledElf() {
    // Generated with Clang 17/LLD from tests/fixtures/original_compiled_guest.c.
    // Machine words in source (not a committed executable binary) allow the
    // same offline integration test to run on Linux and Windows CI.
    constexpr std::array<std::uint32_t, 90> code = {
        0xd100c3ff, 0xa9027bfd, 0x910083fd, 0xd503201f, 0x10008ac0, 0xaa0003e8,
        0xf90007e8, 0x528002e1, 0x94000020, 0xb81fc3a0, 0xb85fc3a8, 0x528953e9,
        0x6b090108, 0x54000100, 0x14000001, 0xd2800040, 0xd2800c61, 0xaa1f03e3,
        0xaa0303e2, 0x94000031, 0x14000011, 0xf94007e2, 0xd2800021, 0xaa0103e0,
        0xd28002e3, 0x9400002b, 0xf9000be0, 0xf9400be8, 0xf1005d08, 0x1a9f07e9,
        0x2a0903e8, 0x93407d01, 0xd2800040, 0xaa1f03e3, 0xaa0303e2, 0x94000021,
        0x14000001, 0xa9427bfd, 0x9100c3ff, 0xd65f03c0, 0xd10083ff, 0xf9000fe0,
        0xb90017e1, 0xb90013ff, 0xb9000fff, 0x14000001, 0xb9400fe8, 0xb94017e9,
        0x6b090108, 0x54000202, 0x14000001, 0xf9400fe8, 0xb9400fe9, 0x38696908,
        0xb9400fe9, 0x11000529, 0x1b097d09, 0xb94013e8, 0x0b090108, 0xb90013e8,
        0x14000001, 0xb9400fe8, 0x11000508, 0xb9000fe8, 0x17ffffee, 0xb94013e0,
        0x910083ff, 0xd65f03c0, 0xd10103ff, 0xf9001fe0, 0xf9001be1, 0xf90017e2,
        0xf90013e3, 0xf9401be8, 0xf9000fe8, 0xf94017e8, 0xf9000be8, 0xf94013e8,
        0xf90007e8, 0xf9401fe8, 0xf90003e8, 0xf9400fe0, 0xf9400be1, 0xf94007e2,
        0xf94003e8, 0xd4000001, 0xf9000fe0, 0xf9400fe0, 0x910103ff, 0xd65f03c0
    };
    static_assert(code.size() * 4 == 0x168);
    constexpr std::string_view message = "Compiled C on AArch64!\n";
    static_assert(message.size() == 23);
    constexpr std::size_t kTextFileOffset = 0x1000;
    constexpr std::size_t kDataSegmentOffset = 0x1168;
    std::vector<std::uint8_t> image(kTextFileOffset + kDataSegmentOffset + message.size() + 1);
    image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
    image[4] = 2; image[5] = 1; image[6] = 1;
    Put(image, 16, 2, 2); Put(image, 18, 183, 2); Put(image, 20, 1, 4);
    Put(image, 24, 0x10000, 8); Put(image, 32, 64, 8);
    Put(image, 52, 64, 2); Put(image, 54, 56, 2); Put(image, 56, 1, 2);
    Put(image, 64, 1, 4); Put(image, 68, 5, 4); // PT_LOAD, read+execute
    Put(image, 72, kTextFileOffset, 8); Put(image, 80, 0x10000, 8);
    Put(image, 96, image.size() - kTextFileOffset, 8);
    Put(image, 104, image.size() - kTextFileOffset, 8);
    Put(image, 112, 0x1000, 8);
    for (std::size_t i = 0; i < code.size(); ++i)
        Put(image, kTextFileOffset + 4 * i, code[i], 4);
    for (std::size_t i = 0; i < message.size(); ++i)
        image[kTextFileOffset + kDataSegmentOffset + i] = static_cast<std::uint8_t>(message[i]);
    return image;
}
} // namespace

int main(int argc, char** argv) {
    auto image = OriginalCompiledElf();
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        return out.write(reinterpret_cast<const char*>(image.data()),
                         static_cast<std::streamsize>(image.size())) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using nint2::Access;
    using nint2::ElfError;
    using nint2::GuestMemory;
    using nint2::SessionStop;
    using nint2::SyntheticSession;
    GuestMemory guest(0x400000);
    auto loaded = nint2::LoadElf64(image, guest);
    EXPECT(loaded.error == ElfError::None && loaded.entry == 0x10000);
    EXPECT(guest.Read8(0x11168) == 'C');
    EXPECT(!guest.Write8(0x11168, 'X')); // Constant is read-only after ELF mapping.
    EXPECT(guest.Map(0x20000, 0x1000, Access::Read | Access::Write));
    SyntheticSession program(guest, loaded.entry, 0x21000);
    const auto result = program.Run(3000);
    EXPECT(result.stop == SessionStop::Exited);
    EXPECT(result.exit_code == 0 && result.steps == 519);
    EXPECT(program.Stdout() == "Compiled C on AArch64!\n");
    EXPECT(program.Stderr().empty());
    EXPECT(program.Run(1).steps == 519);

    // A single modified guest constant must change the computed checksum,
    // select exit(99), and suppress stdout.
    image[0x1000 + 0x1168] = 'X';
    GuestMemory tampered(0x400000);
    loaded = nint2::LoadElf64(image, tampered);
    EXPECT(loaded.error == ElfError::None);
    EXPECT(tampered.Map(0x20000, 0x1000, Access::Read | Access::Write));
    SyntheticSession rejected(tampered, loaded.entry, 0x21000);
    const auto rejected_result = rejected.Run(3000);
    EXPECT(rejected_result.stop == SessionStop::Exited);
    EXPECT(rejected_result.exit_code == 99 && rejected.Stdout().empty());

    std::cout << "Original Clang-compiled AArch64 C checksum/write/exit test passed\n";
    return EXIT_SUCCESS;
}
