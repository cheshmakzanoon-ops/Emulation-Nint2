#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "nint2/cpu.hpp"
#include "nint2/elf.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
void Put(std::vector<std::uint8_t>& data, std::size_t offset, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        data[offset+i] = static_cast<std::uint8_t>(value >> (i*8));
}
std::vector<std::uint8_t> OriginalElf() {
    std::vector<std::uint8_t> bytes(0x10c, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    Put(bytes,16,2,2); // ET_EXEC
    Put(bytes,18,183,2); // EM_AARCH64
    Put(bytes,20,1,4);
    Put(bytes,24,0x1000,8); // entry
    Put(bytes,32,64,8); // phoff
    Put(bytes,52,64,2); // ehsize
    Put(bytes,54,56,2); // phentsize
    Put(bytes,56,1,2); // phnum
    Put(bytes,64,1,4); // PT_LOAD
    Put(bytes,68,5,4); // PF_R|PF_X
    Put(bytes,72,0x100,8); // file offset
    Put(bytes,80,0x1000,8); // virtual address
    Put(bytes,96,12,8); // file size
    Put(bytes,104,32,8); // memory size (BSS)
    Put(bytes,112,0x100,8); // alignment
    Put(bytes,0x100,0x528000e0,4); // movz w0, #7
    Put(bytes,0x104,0x11008c00,4); // add w0, w0, #35
    Put(bytes,0x108,0xd4200000,4); // brk #0
    return bytes;
}
} // namespace

int main() {
    using nint2::Access;
    using nint2::CpuInterpreter;
    using nint2::CpuStop;
    using nint2::ElfError;
    using nint2::GuestMemory;
    auto bytes = OriginalElf();
    GuestMemory memory(0x100000);
    const auto good = nint2::LoadElf64(bytes, memory);
    EXPECT(good.error == ElfError::None && good.entry == 0x1000);
    EXPECT(memory.Read32(0x1000) == 0x528000e0);
    EXPECT(memory.Read64(0x100c) == 0); // .bss zeros
    EXPECT(!memory.Write8(0x1000, 1));
    CpuInterpreter cpu;
    cpu.context.pc = good.entry;
    const auto execution = cpu.Run(memory, 10);
    EXPECT(execution.stop == CpuStop::Breakpoint && cpu.context.x[0] == 42);

    // Invalid ELF must never create executable mappings.
    auto invalid = bytes;
    invalid[0] = 0;
    GuestMemory bad_magic(0x100000);
    EXPECT(nint2::LoadElf64(invalid,bad_magic).error == ElfError::InvalidHeader);
    EXPECT(!bad_magic.Fetch32(0x1000).has_value());
    invalid = bytes;
    Put(invalid,18,62,2); // x86_64 not ARM64
    GuestMemory wrong_machine(0x100000);
    EXPECT(nint2::LoadElf64(invalid,wrong_machine).error == ElfError::UnsupportedFormat);
    invalid = bytes;
    Put(invalid,32,UINT64_MAX - 10,8);
    GuestMemory ph_overflow(0x100000);
    EXPECT(nint2::LoadElf64(invalid,ph_overflow).error == ElfError::InvalidHeader);
    invalid = bytes;
    Put(invalid,68,7,4); // PF_W|PF_X
    GuestMemory wx(0x100000);
    EXPECT(nint2::LoadElf64(invalid,wx).error == ElfError::InvalidSegment);
    invalid = bytes;
    Put(invalid,104,3,8); // memsz smaller than filesz
    GuestMemory reversed(0x100000);
    EXPECT(nint2::LoadElf64(invalid,reversed).error == ElfError::InvalidSegment);
    invalid = bytes;
    Put(invalid,72,0xffffffffU,8); // PT_LOAD outside file
    GuestMemory outside(0x100000);
    EXPECT(nint2::LoadElf64(invalid,outside).error == ElfError::InvalidSegment);
    invalid = bytes;
    Put(invalid,104,0x10000000,8); // unreasonable virtual map
    GuestMemory huge(0x100000000ULL);
    EXPECT(nint2::LoadElf64(invalid,huge).error == ElfError::ResourceLimit);
    invalid = bytes;
    Put(invalid,24,0x2000,8); // entry not in executable segment
    GuestMemory wrong_entry(0x100000);
    EXPECT(nint2::LoadElf64(invalid,wrong_entry).error == ElfError::NoExecutableEntry);
    invalid = bytes;
    Put(invalid,64,2,4); // dynamic linker unsupported
    GuestMemory dynamic(0x100000);
    EXPECT(nint2::LoadElf64(invalid,dynamic).error == ElfError::UnsupportedFormat);
    GuestMemory occupied(0x100000);
    EXPECT(occupied.Map(0x1000,4096,Access::Read));
    EXPECT(nint2::LoadElf64(bytes,occupied).error == ElfError::MappingFailed);
    EXPECT(!occupied.Fetch32(0x1000).has_value());

    // Deterministic malformed input corpus (single-byte corruptions / truncation).
    std::uint32_t state = 0x13579bdf;
    for (unsigned i = 0; i < 256; ++i) {
        state = state * 1664525U + 1013904223U;
        auto corrupted = bytes;
        corrupted[state % corrupted.size()] ^= static_cast<std::uint8_t>((state >> 16) | 1);
        GuestMemory sandbox(0x100000);
        const auto result = nint2::LoadElf64(corrupted,sandbox);
        if (result.error == ElfError::None) {
            CpuInterpreter test_cpu;
            test_cpu.context.pc = result.entry;
            (void)test_cpu.Run(sandbox,16);
        }
    }
    for (std::size_t size = 0; size < 64; ++size) {
        GuestMemory sandbox(0x100000);
        EXPECT(nint2::LoadElf64({bytes.data(),size},sandbox).error != ElfError::None);
    }
    std::cout << "Original ELF64/AArch64 load/run and malformed corpus passed\n";
    return EXIT_SUCCESS;
}
