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
    std::cerr << "FAILED: " << #condition << " at line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while(false)

namespace {
void Put(std::vector<std::uint8_t>& out, std::size_t pos, std::uint64_t val, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out[pos + i] = static_cast<std::uint8_t>(val >> (i * 8));
}
std::vector<std::uint8_t> OriginalQueueElf() {
    // Independently assembled with clang --target=aarch64-none-elf; the source
    // is in tests/fixtures/original_queue.s. ELF64 has two loadable segments:
    // a read+execute text page and a separate writable .bss receive buffer.
    constexpr std::array<std::uint32_t, 25> code = {
        0xd2800088, 0xd2800040, 0xd4000001, 0xaa0003f3, 0xd28000a8,
        0xaa1303e0, 0x10000261, 0xd2800242, 0xd4000001, 0xd28000c8,
        0xaa1303e0, 0x1000fea1, 0xd2800242, 0xd4000001, 0xd2800028,
        0xd2800020, 0x1000fe01, 0xd2800242, 0xd4000001, 0xd28000e8,
        0xaa1303e0, 0xd4000001, 0xd2800048, 0xd2800000, 0xd4000001
    };
    constexpr std::string_view message = "Hello via kernel!\n";
    static_assert(message.size() == 18);
    std::vector<std::uint8_t> elf(0x2000);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2; elf[5] = 1; elf[6] = 1;
    Put(elf, 16, 2, 2); Put(elf, 18, 183, 2); Put(elf, 20, 1, 4);
    Put(elf, 24, 0x10000, 8); Put(elf, 32, 64, 8);
    Put(elf, 52, 64, 2); Put(elf, 54, 56, 2); Put(elf, 56, 2, 2);
    Put(elf, 64, 1, 4); Put(elf, 68, 5, 4);
    Put(elf, 72, 0x1000, 8); Put(elf, 80, 0x10000, 8);
    Put(elf, 96, code.size()*4+message.size(), 8);
    Put(elf, 104, code.size()*4+message.size(), 8);
    Put(elf, 112, 0x1000, 8);
    Put(elf, 120, 1, 4); Put(elf, 124, 6, 4);
    Put(elf, 128, 0x2000, 8); Put(elf, 136, 0x12000, 8);
    Put(elf, 152, 0, 8); Put(elf, 160, 64, 8);
    Put(elf, 168, 0x1000, 8);
    for (std::size_t i=0; i<code.size(); ++i) Put(elf, 0x1000+4*i, code[i], 4);
    for (std::size_t i=0; i<message.size(); ++i)
        elf[0x1000+4*code.size()+i] = static_cast<std::uint8_t>(message[i]);
    return elf;
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--emit-elf") {
        const auto elf = OriginalQueueElf();
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        return out.write(reinterpret_cast<const char*>(elf.data()),
                         static_cast<std::streamsize>(elf.size())) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    using nint2::Access;
    using nint2::GuestMemory;
    using nint2::SessionStop;
    using nint2::SyntheticSession;
    auto binary = OriginalQueueElf();
    GuestMemory guest(0x400000);
    const auto loaded = nint2::LoadElf64(binary, guest);
    EXPECT(loaded.error == nint2::ElfError::None);
    EXPECT(loaded.entry == 0x10000);
    EXPECT(guest.Read8(0x12000) == 0);
    EXPECT(!guest.Write8(0x10000, 0));
    SyntheticSession program(guest, loaded.entry, 0x30000);
    const auto result = program.Run(256);
    EXPECT(result.stop == SessionStop::Exited);
    EXPECT(result.exit_code == 0 && result.steps == 25);
    EXPECT(program.Stdout() == "Hello via kernel!\n");
    EXPECT(program.HandleCount() == 0);
    EXPECT(guest.Read8(0x12000) == 'H' && guest.Read8(0x12011) == '\n');
    EXPECT(program.Run(1).steps == 25);

    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 4096, Access::Read | Access::Write));
    EXPECT(memory.Write32(0x1000, 0xd4000001));
    EXPECT(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    EXPECT(memory.Map(0x2000, 4096, Access::Read | Access::Write));
    EXPECT(memory.Write8(0x2000, 'A'));
    EXPECT(memory.Write8(0x2001, 'B'));
    EXPECT(memory.Write8(0x2002, 'C'));
    EXPECT(memory.Write8(0x2fff, 'X'));
    SyntheticSession session(memory, 0x1000, 0x3000);
    auto call = [&](std::uint64_t service, std::uint64_t x0, std::uint64_t x1 = 0, std::uint64_t x2 = 0) {
        auto& ctx = session.Context();
        ctx.pc = 0x1000;
        ctx.x[8] = service;
        ctx.x[0] = x0;
        ctx.x[1] = x1;
        ctx.x[2] = x2;
        const auto done = session.Run(1);
        if (done.stop != SessionStop::StepLimit) std::abort();
        return ctx.x[0];
    };
    constexpr auto neg = [](int value) { return static_cast<std::uint64_t>(-static_cast<std::int64_t>(value)); };
    EXPECT(call(4, 0) == neg(22));
    const auto handle = call(4, 1);
    EXPECT(handle == 1);
    EXPECT(call(5, handle, 0x2000, 3) == 3);
    EXPECT(call(5, handle, 0x2000, 3) == neg(11));
    EXPECT(call(6, handle, 0x2fff, 3) == neg(14));
    EXPECT(memory.Read8(0x2fff) == 'X');
    EXPECT(call(6, handle, 0x3000, 3) == neg(14));
    EXPECT(call(6, handle, 0x2100, 2) == neg(90));
    EXPECT(call(6, handle, 0x2100, 3) == 3);
    EXPECT(memory.Read8(0x2100) == 'A' && memory.Read8(0x2102) == 'C');
    EXPECT(call(6, handle, 0x2100, 3) == neg(11));
    EXPECT(call(5, handle, UINT64_MAX, 3) == neg(14));
    EXPECT(call(5, handle, 0x2000, 4097) == neg(22));
    const auto sender = call(8, handle, 1);
    EXPECT(sender == 2);
    EXPECT(call(8, sender, 1) == neg(13));
    EXPECT(call(6, sender, 0x2100, 3) == neg(13));
    EXPECT(call(7, handle) == 0);
    EXPECT(call(7, handle) == neg(9));
    EXPECT(call(5, sender, 0x2000, 3) == 3);
    EXPECT(call(7, sender) == 0);
    EXPECT(call(5, sender, 0x2000, 3) == neg(9));
    EXPECT(call(4, 1) == 3);
    EXPECT(call(7, 3) == 0);
    EXPECT(session.HandleCount() == 0);

    EXPECT(memory.Map(0x3000, 4096, Access::Read | Access::Write));
    const std::array<std::uint8_t, 4> into{'1','2','3','4'};
    EXPECT(memory.WriteBuffer(0x3ffe, into) == false);
    EXPECT(memory.Read8(0x3ffe) == 0 && memory.Read8(0x3fff) == 0);
    EXPECT(memory.Map(0x4000, 4096, Access::Read | Access::Write));
    EXPECT(memory.WriteBuffer(0x3ffe, into));
    EXPECT(memory.Read8(0x3ffe) == '1' && memory.Read8(0x4001) == '4');
    std::cout << "Original AArch64 ELF queue/send/receive/write/exit and faults passed\n";
    return EXIT_SUCCESS;
}
