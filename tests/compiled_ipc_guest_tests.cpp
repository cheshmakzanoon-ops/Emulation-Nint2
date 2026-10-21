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
    std::cerr << "FAILED " << #condition << " at line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while(false)

namespace {
void Put(std::vector<std::uint8_t>& out, std::size_t pos, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        out[pos + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> OriginalCompiledIpcElf() {
    // Clang 17/LLD --target=aarch64-none-elf -O0 linked original_compiled_ipc.c.
    // Only original machine words and constants are embedded; no binaries,
    // copyrighted console software, tools, firmware, keys or proprietary SDKs.
    constexpr std::array<std::uint32_t, 168> code = {
        0xd10103ff, 0xa9037bfd, 0x9100c3fd, 0xd503201f, 0x10009488, 0xf90007e8, 0xd503201f, 0x50011568,
        0xf9000be8, 0x9400006b, 0x3101d008, 0x54000100, 0x14000001, 0xd2800040, 0xd2800b61, 0xaa1f03e3,
        0xaa0303e2, 0x94000081, 0x1400005f, 0xd2800080, 0xd2800041, 0xaa1f03e3, 0xaa0303e2, 0x9400007b,
        0xf81f83a0, 0xf85f83a8, 0xf1000108, 0x5400010c, 0x14000001, 0xd2800040, 0xd2800b81, 0xaa1f03e3,
        0xaa0303e2, 0x94000071, 0x1400004f, 0xf94007e2, 0xd28002c3, 0xf81f03a3, 0xf85f83a1, 0xd28000a0,
        0x9400006a, 0xf1005808, 0x54000141, 0x14000001, 0xf9400be2, 0xf85f83a1, 0xd28000c0, 0xd28002e3,
        0x94000062, 0xf1005808, 0x54000100, 0x14000001, 0xd2800040, 0xd2800ba1, 0xaa1f03e3, 0xaa0303e2,
        0x9400005a, 0x14000038, 0xf9000fff, 0x14000001, 0xf9400fe8, 0xf1005908, 0x5400030a, 0x14000001,
        0xf9400fe9, 0xd503201f, 0x50010e08, 0x38696908, 0xf9400fea, 0xd503201f, 0x10008c49, 0x386a6929,
        0x6b090108, 0x54000100, 0x14000001, 0xd2800040, 0xd2800bc1, 0xaa1f03e3, 0xaa0303e2, 0x94000043,
        0x14000021, 0x14000001, 0xf9400fe8, 0x91000508, 0xf9000fe8, 0x17ffffe7, 0xf9400be2, 0xd2800021,
        0xaa0103e0, 0xd28002c3, 0x94000038, 0xf1005808, 0x54000121, 0x14000001, 0xf85f83a1, 0xd28000e0,
        0xaa1f03e3, 0xaa0303e2, 0x94000030, 0xb4000100, 0x14000001, 0xd2800040, 0xd2800be1, 0xaa1f03e3,
        0xaa0303e2, 0x94000029, 0x14000007, 0xd2800040, 0xaa1f03e3, 0xaa0303e1, 0xaa0303e2, 0x94000023,
        0x14000001, 0xa9437bfd, 0x910103ff, 0xd65f03c0, 0xd10043ff, 0xb9000fff, 0xb9000bff, 0x14000001,
        0xb9400be8, 0x71001908, 0x540002a2, 0x14000001, 0xb9400be8, 0x2a0803e9, 0xb0000008, 0x910add08,
        0x38e96908, 0xb9400be9, 0x2a0903ea, 0xb0000009, 0x910af929, 0x78ea7929, 0x1b097d09, 0xb9400fe8,
        0x0b090108, 0xb9000fe8, 0x14000001, 0xb9400be8, 0x11000508, 0xb9000be8, 0x17ffffea, 0xb9400fe0,
        0x910043ff, 0xd65f03c0, 0xd10103ff, 0xf9001fe0, 0xf9001be1, 0xf90017e2, 0xf90013e3, 0xf9401be8,
        0xf9000fe8, 0xf94017e8, 0xf9000be8, 0xf94013e8, 0xf90007e8, 0xf9401fe8, 0xf90003e8, 0xf9400fe0,
        0xf9400be1, 0xf94007e2, 0xf94003e8, 0xd4000001, 0xf9000fe0, 0xf9400fe0, 0x910103ff, 0xd65f03c0,
    };
    constexpr std::string_view message = "C guest IPC verified!\n";
    constexpr std::array<std::int8_t, 6> changes{-3, 4, -1, 8, -5, 9};
    constexpr std::array<std::int16_t, 6> multipliers{10, -2, 7, -4, 6, -1};
    static_assert(code.size() * 4 == 0x2a0 && message.size() == 22);

    // Keep compiler-linked text addresses: .text @ 0x10000 (0x2a0 bytes),
    // .rodata @ 0x112a0 (0x2a bytes), .bss @ 0x122ca (23 bytes).
    // Coalesce the first two (non-overlapping) pages into one RX ELF segment;
    // separate writable BSS has no file backing. All ELF bytes generated at
    // test time; no compiled binary is stored in the repository.
    constexpr std::size_t file_offset = 0x1000;
    constexpr std::size_t rodata_offset = 0x12a0;
    std::vector<std::uint8_t> elf(0x3000, 0);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2; elf[5] = 1; elf[6] = 1;
    Put(elf, 16, 2, 2); Put(elf, 18, 183, 2); Put(elf, 20, 1, 4);
    Put(elf, 24, 0x10000, 8); Put(elf, 32, 64, 8);
    Put(elf, 52, 64, 2); Put(elf, 54, 56, 2); Put(elf, 56, 2, 2);
    Put(elf, 64, 1, 4); Put(elf, 68, 5, 4); // segment RX
    Put(elf, 72, file_offset, 8); Put(elf, 80, 0x10000, 8);
    Put(elf, 96, rodata_offset + 42, 8); Put(elf, 104, rodata_offset + 42, 8);
    Put(elf, 112, 0x1000, 8);
    Put(elf, 120, 1, 4); Put(elf, 124, 6, 4); // segment RW, zero-backed
    Put(elf, 128, 0x3000, 8); Put(elf, 136, 0x12000, 8);
    Put(elf, 152, 0, 8); Put(elf, 160, 0x1000, 8);
    Put(elf, 168, 0x1000, 8);
    for (std::size_t i = 0; i < code.size(); ++i)
        Put(elf, file_offset + i * 4, code[i], 4);
    const auto ro = file_offset + rodata_offset;
    for (std::size_t i = 0; i < message.size(); ++i)
        elf[ro + i] = static_cast<std::uint8_t>(message[i]);
    for (std::size_t i = 0; i < changes.size(); ++i)
        elf[ro + 23 + i] = static_cast<std::uint8_t>(changes[i]);
    for (std::size_t i = 0; i < multipliers.size(); ++i)
        Put(elf, ro + 30 + i * 2, static_cast<std::uint16_t>(multipliers[i]), 2);
    return elf;
}
} // namespace

int main(int argc, char** argv) {
    auto binary = OriginalCompiledIpcElf();
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        return out.write(reinterpret_cast<const char*>(binary.data()),
                         static_cast<std::streamsize>(binary.size())) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using nint2::Access;
    using nint2::ElfError;
    using nint2::GuestMemory;
    using nint2::SessionStop;
    using nint2::SyntheticSession;
    GuestMemory memory(0x400000);
    auto loaded = nint2::LoadElf64(binary, memory);
    EXPECT(loaded.error == ElfError::None && loaded.entry == 0x10000);
    EXPECT(memory.Read8(0x112b7) == 0xfd);
    EXPECT(!memory.Write8(0x112b7, 3)); // source data is read-only
    EXPECT(memory.Read8(0x122ca) == 0); // writable zero-initialized .bss
    EXPECT(memory.Map(0x30000, 0x1000, Access::Read | Access::Write));
    SyntheticSession session(memory, loaded.entry, 0x31000);
    const auto result = session.Run(6000);
    EXPECT(result.stop == SessionStop::Exited);
    EXPECT(result.exit_code == 0 && result.steps > 100 && result.steps < 6000);
    EXPECT(session.Stdout() == "C guest IPC verified!\n");
    EXPECT(session.Stderr().empty() && session.HandleCount() == 0);
    EXPECT(memory.Read8(0x122ca) == 'C' && memory.Read8(0x122ca + 21) == '\n');
    EXPECT(session.Run(1).steps == result.steps);

    // Tamper a single signed-byte constant: original C must take error path
    // before allocating any queue. No fake success or output is permitted.
    binary[0x1000 + 0x12a0 + 23] = 3;
    GuestMemory tampered(0x400000);
    loaded = nint2::LoadElf64(binary, tampered);
    EXPECT(loaded.error == ElfError::None);
    EXPECT(tampered.Map(0x30000, 0x1000, Access::Read | Access::Write));
    SyntheticSession rejected(tampered, loaded.entry, 0x31000);
    const auto negative = rejected.Run(6000);
    EXPECT(negative.stop == SessionStop::Exited && negative.exit_code == 91);
    EXPECT(rejected.Stdout().empty() && rejected.HandleCount() == 0);
    std::cout << "Clang-generated AArch64 C signed arithmetic and queue round-trip passed\n";
    return EXIT_SUCCESS;
}
