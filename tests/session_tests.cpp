#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/session.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
void Put(std::vector<std::uint8_t>& data, std::size_t offset, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        data[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> AuthoredHelloElf() {
    // Hand-built ET_EXEC container holding original Clang-assembled AArch64
    // words (tests/fixtures/original_hello.s). No console software or assets.
    constexpr std::array<std::uint32_t, 8> code = {
        0xd2800028, // movz x8,#1: synthetic write
        0xd2800020, // movz x0,#1: stdout
        0x100000c1, // adr x1, message
        0xd2800282, // movz x2,#20
        0xd4000001, // svc #0: write
        0xd2800048, // movz x8,#2: synthetic exit
        0xd2800000, // movz x0,#0: success
        0xd4000001  // svc #0: exit
    };
    constexpr std::array<std::uint8_t, 20> message = {
        'H','e','l','l','o',' ','f','r','o','m',' ',
        'A','A','r','c','h','6','4','!','\n'
    };
    std::vector<std::uint8_t> image(0x100 + code.size() * 4 + message.size());
    image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
    image[4] = 2; image[5] = 1; image[6] = 1;
    Put(image, 16, 2, 2); Put(image, 18, 183, 2);
    Put(image, 20, 1, 4); Put(image, 24, 0x10000, 8);
    Put(image, 32, 64, 8); Put(image, 52, 64, 2);
    Put(image, 54, 56, 2); Put(image, 56, 1, 2);
    Put(image, 64, 1, 4); Put(image, 68, 5, 4);
    Put(image, 72, 0x100, 8); Put(image, 80, 0x10000, 8);
    Put(image, 96, image.size() - 0x100, 8);
    Put(image, 104, image.size() - 0x100, 8);
    Put(image, 112, 0x100, 8);
    for (std::size_t i = 0; i < code.size(); ++i)
        Put(image, 0x100 + i*4, code[i], 4);
    for (std::size_t i = 0; i < message.size(); ++i)
        image[0x100 + code.size()*4 + i] = message[i];
    return image;
}
} // namespace

int main() {
    using nint2::Access;
    using nint2::CpuStop;
    using nint2::ElfError;
    using nint2::GuestMemory;
    using nint2::SessionStop;
    using nint2::SyntheticSession;
    const auto elf = AuthoredHelloElf();
    GuestMemory executable(0x40000);
    const auto loaded = nint2::LoadElf64(elf, executable);
    EXPECT(loaded.error == ElfError::None && loaded.entry == 0x10000);
    EXPECT(executable.Map(0x30000, 4096, Access::Read | Access::Write));
    SyntheticSession hello(executable, loaded.entry, 0x31000);
    const auto done = hello.Run(32);
    EXPECT(done.stop == SessionStop::Exited);
    EXPECT(done.exit_code == 0 && done.steps == 8);
    EXPECT(hello.Stdout() == "Hello from AArch64!\n");
    EXPECT(hello.Stderr().empty());
    EXPECT(hello.Context().pc == 0x1001c); // Exit traps at SVC
    EXPECT(hello.Run(1).stop == SessionStop::Exited);
    EXPECT(hello.Run(1).steps == 8); // exited session must remain halted

    GuestMemory memory(0x10000);
    EXPECT(memory.Map(0x1000, 4096, Access::Read | Access::Write));
    EXPECT(memory.Write32(0x1000, 0xd4000001)); // SVC #0
    EXPECT(memory.Write32(0x1004, 0xd4000021)); // SVC #1 (unsupported)
    EXPECT(memory.Write32(0x1008, 0x14000000)); // B to same PC
    EXPECT(memory.Write32(0x100c, 0));           // undefined instruction
    EXPECT(memory.Map(0x2000, 4096, Access::Read | Access::Write));
    EXPECT(memory.Write8(0x2000, 'A'));
    EXPECT(memory.Write8(0x2001, 'B'));
    EXPECT(memory.Write8(0x2fff, 'C'));
    EXPECT(memory.Protect(0x1000, 4096, Access::Read | Access::Execute));
    SyntheticSession session(memory, 0x1000, 0x3000);
    auto& regs = session.Context();
    regs.x[8] = 1; regs.x[0] = 1; regs.x[1] = 0x2000; regs.x[2] = 2;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == 2 && session.Stdout() == "AB");
    regs.pc = 0x1000;
    regs.x[8] = 1; regs.x[0] = 2; regs.x[1] = 0x2000; regs.x[2] = 1;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == 1 && session.Stderr() == "A");
    regs.pc = 0x1000;
    regs.x[8] = 1; regs.x[0] = 99; regs.x[1] = 0x2000; regs.x[2] = 1;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == static_cast<std::uint64_t>(-9LL)); // EBADF
    regs.pc = 0x1000;
    regs.x[8] = 1; regs.x[0] = 1; regs.x[1] = 0x2fff; regs.x[2] = 2;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == static_cast<std::uint64_t>(-14LL)); // EFAULT
    EXPECT(session.Stdout() == "AB"); // invalid second byte: no partial append
    regs.pc = 0x1000;
    regs.x[8] = 1; regs.x[0] = 1; regs.x[1] = UINT64_MAX; regs.x[2] = 2;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == static_cast<std::uint64_t>(-14LL)); // pointer overflow
    regs.pc = 0x1000;
    regs.x[8] = 1; regs.x[0] = 1; regs.x[1] = 0x2000; regs.x[2] = 4097;
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == static_cast<std::uint64_t>(-22LL)); // EINVAL
    regs.pc = 0x1000; regs.x[8] = 3; // deterministic virtual instruction ticks
    EXPECT(session.Run(1).stop == SessionStop::StepLimit);
    EXPECT(regs.x[0] == 7);
    regs.pc = 0x1000; regs.x[8] = 199;
    const auto unknown = session.Run(5);
    EXPECT(unknown.stop == SessionStop::UnsupportedSupervisorCall);
    EXPECT(unknown.service_number == 199 && regs.pc == 0x1000);
    regs.pc = 0x1004; regs.x[8] = 1;
    const auto imm = session.Run(5);
    EXPECT(imm.stop == SessionStop::UnsupportedSupervisorCall);
    EXPECT(regs.pc == 0x1004);
    regs.pc = 0x1008;
    EXPECT(session.Run(17).stop == SessionStop::StepLimit); // branch loop bounded
    EXPECT(regs.pc == 0x1008);
    regs.pc = 0x100c;
    const auto invalid = session.Run(2);
    EXPECT(invalid.stop == SessionStop::CpuException);
    EXPECT(invalid.cpu_stop == CpuStop::UndefinedInstruction);
    EXPECT(regs.pc == 0x100c);
    regs.pc = 0x1000;
    regs.x[8] = 2; regs.x[0] = 37;
    const auto exit = session.Run(1);
    EXPECT(exit.stop == SessionStop::Exited && exit.exit_code == 37);

    // Output accounting is checked across calls and both output streams.
    GuestMemory budget_mem(0x10000);
    EXPECT(budget_mem.Map(0x1000,4096,Access::Read | Access::Write));
    EXPECT(budget_mem.Write32(0x1000,0xd4000001));
    EXPECT(budget_mem.Protect(0x1000,4096,Access::Read | Access::Execute));
    EXPECT(budget_mem.Map(0x2000,4096,Access::Read | Access::Write));
    SyntheticSession budget(budget_mem,0x1000,0x3000);
    for (unsigned i=0; i<16; ++i) {
        auto& b = budget.Context();
        b.pc = 0x1000;
        b.x[8] = 1; b.x[0] = (i%2)+1; b.x[1] = 0x2000; b.x[2] = 4096;
        EXPECT(budget.Run(1).stop == SessionStop::StepLimit);
        EXPECT(b.x[0] == 4096);
    }
    EXPECT(budget.Stdout().size() == 8*4096);
    EXPECT(budget.Stderr().size() == 8*4096);
    auto& b = budget.Context();
    b.pc=0x1000; b.x[8]=1; b.x[0]=1; b.x[1]=0x2000; b.x[2]=1;
    EXPECT(budget.Run(1).stop == SessionStop::StepLimit);
    EXPECT(b.x[0] == static_cast<std::uint64_t>(-28LL)); // ENOSPC
    EXPECT(budget.Stdout().size() == 8*4096);

    std::cout << "Synthetic AArch64 ELF -> SVC write -> exit passed\n";
    return EXIT_SUCCESS;
}
