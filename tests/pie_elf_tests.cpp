#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "nint2/cpu.hpp"
#include "nint2/elf.hpp"

// Entirely original synthetic ET_DYN image: generated in test memory, no
// firmware, console keys, toolchain downloads or host system call dependency.
#define EXPECT(test) do { if (!(test)) { \
    std::cerr << "FAIL " << #test << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::Access;
using nint2::ElfError;
using nint2::GuestMemory;

void Write(std::vector<std::uint8_t>& d, std::size_t off,
           std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        d.at(off+i) = static_cast<std::uint8_t>(value >> (8*i));
}
[[nodiscard]] std::vector<std::uint8_t> OriginalPie() {
    std::vector<std::uint8_t> b(0x500,0);
    b[0]=0x7f; b[1]='E'; b[2]='L'; b[3]='F';
    b[4]=2; b[5]=1; b[6]=1;
    Write(b,16,3,2);      // ET_DYN: freestanding static position-independent
    Write(b,18,183,2);    // EM_AARCH64
    Write(b,20,1,4);      // ELF_VERSION
    Write(b,24,0x10260,8);// entry in executable code segment
    Write(b,32,64,8);     // e_phoff
    Write(b,52,64,2);     // e_ehsize
    Write(b,54,56,2);     // e_phentsize
    Write(b,56,3,2);      // e_phnum

    // PT_LOAD RX: code does ADRP; LDR pointer; LDR value; BRK.
    Write(b,64,1,4);
    Write(b,68,5,4);
    Write(b,72,0x260,8);
    Write(b,80,0x10260,8);
    Write(b,96,16,8);
    Write(b,104,16,8);
    Write(b,112,0x100,8);
    // Independently assembled AArch64 subset, no HLE or host shortcut.
    Write(b,0x260,0x90000088,4); // adrp x8, +0x10000
    Write(b,0x264,0xf941d108,4); // ldr x8,[x8,#0x3a0]
    Write(b,0x268,0xf9400100,4); // ldr x0,[x8]
    Write(b,0x26c,0xd4200000,4); // brk #0

    // PT_LOAD RW: self-contained dynamic table, RELA, pointer and counter.
    Write(b,120,1,4);
    Write(b,124,6,4);
    Write(b,128,0x300,8);
    Write(b,136,0x20300,8);
    Write(b,152,0x200,8);
    Write(b,160,0x300,8);
    Write(b,168,0x100,8);

    // PT_DYNAMIC must be both file backed and in a PT_LOAD segment.
    Write(b,176,2,4);
    Write(b,184,0x300,8);
    Write(b,192,0x20300,8);
    Write(b,208,64,8);
    Write(b,216,64,8);
    Write(b,224,8,8);
    Write(b,0x300,7,8);       // DT_RELA
    Write(b,0x308,0x20380,8);
    Write(b,0x310,8,8);       // DT_RELASZ
    Write(b,0x318,24,8);
    Write(b,0x320,9,8);       // DT_RELAENT
    Write(b,0x328,24,8);
    // DT_NULL at 0x330, naturally zero-initialized

    Write(b,0x380,0x203a0,8); // r_offset
    Write(b,0x388,1027,8);    // R_AARCH64_RELATIVE, sym=0
    Write(b,0x390,0x203a8,8); // signed addend
    Write(b,0x3a8,19,8);      // original guest data
    return b;
}
[[nodiscard]] std::vector<std::uint8_t> OriginalExec() {
    auto b=OriginalPie();
    Write(b,16,2,2);
    Write(b,176,0,4); // no PT_DYNAMIC for ET_EXEC
    // With a fixed address, don't apply relocations in ELF processing.
    return b;
}
[[nodiscard]] bool NoPartialMapping(const GuestMemory& m, std::uint64_t base=0x200000) {
    return !m.Fetch32(base+0x260) && !m.Read64(base+0x103a0);
}
[[nodiscard]] bool Execute(const std::vector<std::uint8_t>& image,
                           std::uint64_t base,
                           std::uint64_t counter=19) {
    GuestMemory memory(0x1000000);
    const auto loaded=nint2::LoadElf64(image,memory,base);
    if (loaded.error!=ElfError::None ||
        loaded.load_bias!=base-0x10000 ||
        loaded.entry!=base+0x260) return false;
    if (memory.Read64(base+0x103a0)!=base+0x103a8 ||
        memory.Read64(base+0x103a8)!=counter) return false;
    if (memory.Write8(base+0x260,0) || memory.Fetch32(base+0x103a0))
        return false;
    nint2::CpuInterpreter cpu;
    cpu.context.pc=loaded.entry;
    const auto result=cpu.Run(memory,12);
    return result.stop==nint2::CpuStop::Breakpoint &&
           cpu.context.x[0]==counter &&
           cpu.context.pc==base+0x26c;
}
} // namespace

int main() {
    auto bytes=OriginalPie();
    EXPECT(Execute(bytes,0x200000));
    EXPECT(Execute(bytes,0x300000));
    EXPECT(Execute(bytes,0x600000));

    // The loaded executable is independent of the caller-owned file bytes.
    {
        GuestMemory memory(0x800000);
        auto image=OriginalPie();
        const auto loaded=nint2::LoadElf64(image,memory);
        EXPECT(loaded.error==ElfError::None);
        EXPECT(loaded.entry==0x200260 && loaded.load_bias==0x1f0000);
        image.clear();
        EXPECT(memory.Read64(0x2103a0)==0x2103a8);
        EXPECT(memory.Read64(0x2103a8)==19);
        EXPECT(memory.Read64(0x210400)==0); // guest BSS is zero
        EXPECT(!memory.Read8(0x200000-4096));
        EXPECT(!memory.Write64(0x200260,0)); // RX and W^X final protection
        EXPECT(memory.Write64(0x2103a8,33));
        nint2::CpuInterpreter cpu;
        cpu.context.pc=loaded.entry;
        EXPECT(cpu.Run(memory,12).stop==nint2::CpuStop::Breakpoint);
        EXPECT(cpu.context.x[0]==33);
    }
    // ET_EXEC retains original entry/addresses and must reject PT_DYNAMIC.
    {
        GuestMemory memory(0x800000);
        auto exec=OriginalExec();
        EXPECT(nint2::LoadElf64(exec,memory).error==ElfError::None);
        EXPECT(memory.Fetch32(0x10260)==0x90000088);
        EXPECT(memory.Read64(0x203a8)==19);
        EXPECT(!memory.Fetch32(0x20260));
        Write(exec,176,2,4);
        GuestMemory invalid(0x800000);
        EXPECT(nint2::LoadElf64(exec,invalid).error==ElfError::UnsupportedFormat);
    }
    // Fixed base conflicts cannot silently clobber already loaded program.
    {
        GuestMemory memory(0x800000);
        EXPECT(memory.Map(0x210000,4096,Access::Read|Access::Write));
        EXPECT(memory.Write64(0x2103a0,0xcafebabe));
        EXPECT(nint2::LoadElf64(bytes,memory).error==ElfError::MappingFailed);
        EXPECT(memory.Read64(0x2103a0)==0xcafebabe);
        EXPECT(!memory.Fetch32(0x200260));
    }
    {
        GuestMemory small(0x210000);
        EXPECT(nint2::LoadElf64(bytes,small).error==ElfError::MappingFailed);
        EXPECT(!small.Fetch32(0x200260));
        GuestMemory unaligned(0x800000);
        EXPECT(nint2::LoadElf64(bytes,unaligned,0x200001).error==ElfError::InvalidSegment);
        EXPECT(NoPartialMapping(unaligned));
    }

    struct Mutation {
        const char* name;
        std::size_t offset;
        unsigned width;
        std::uint64_t value;
        ElfError expected;
    };
    const std::array<Mutation,21> invalid{{
        {"needed",0x300,8,1,ElfError::UnsupportedRelocation},
        {"plt",0x300,8,23,ElfError::UnsupportedRelocation},
        {"relr",0x300,8,36,ElfError::UnsupportedRelocation},
        {"tls",0x300,8,17,ElfError::UnsupportedRelocation},
        {"textrel",0x300,8,22,ElfError::UnsupportedRelocation},
        {"missing NULL",0x330,8,1,ElfError::UnsupportedRelocation},
        {"duplicate RELA",0x310,8,7,ElfError::InvalidRelocation},
        {"bad rela ent",0x328,8,32,ElfError::InvalidRelocation},
        {"bad rela size",0x318,8,23,ElfError::InvalidRelocation},
        {"rela outside",0x308,8,0x500000,ElfError::InvalidRelocation},
        {"nonrelative type",0x388,8,1025,ElfError::UnsupportedRelocation},
        {"symbol",0x388,8,0x100000403ULL,ElfError::UnsupportedRelocation},
        {"RX target",0x380,8,0x10260,ElfError::InvalidRelocation},
        {"misaligned target",0x380,8,0x203a1,ElfError::InvalidRelocation},
        {"outside target",0x380,8,0x20900,ElfError::InvalidRelocation},
        {"negative too large",0x390,8,UINT64_MAX-0x1f0000,ElfError::InvalidRelocation},
        {"pointer outside",0x390,8,0x800000,ElfError::InvalidRelocation},
        {"entry RW",24,8,0x203a0,ElfError::NoExecutableEntry},
        {"entry misaligned",24,8,0x10261,ElfError::NoExecutableEntry},
        {"text WX",68,4,7,ElfError::InvalidSegment},
        {"data WX",124,4,7,ElfError::InvalidSegment}
    }};
    for (const auto& t : invalid) {
        auto copy=bytes;
        Write(copy,t.offset,t.value,t.width);
        GuestMemory memory(0x800000);
        const auto result=nint2::LoadElf64(copy,memory);
        if (result.error!=t.expected) {
            std::cerr << t.name << ": got " << nint2::ElfErrorName(result.error)
                      << " wanted " << nint2::ElfErrorName(t.expected) << '\n';
            return EXIT_FAILURE;
        }
        EXPECT(NoPartialMapping(memory));
    }
    // Bad/dangling PT_DYNAMIC ranges must not be trusted as file pointers.
    {
        auto broken=bytes;
        Write(broken,184,0x420,8);
        GuestMemory memory(0x800000);
        EXPECT(nint2::LoadElf64(broken,memory).error==ElfError::InvalidRelocation);
        EXPECT(NoPartialMapping(memory));
    }
    {
        auto broken=bytes;
        Write(broken,192,0x40000,8);
        GuestMemory memory(0x800000);
        EXPECT(nint2::LoadElf64(broken,memory).error==ElfError::InvalidRelocation);
        EXPECT(NoPartialMapping(memory));
    }
    {
        auto broken=bytes;
        Write(broken,176,3,4); // PT_INTERP
        GuestMemory memory(0x800000);
        EXPECT(nint2::LoadElf64(broken,memory).error==ElfError::UnsupportedFormat);
    }
    {
        auto broken=bytes;
        Write(broken,176,7,4); // PT_TLS
        GuestMemory memory(0x800000);
        EXPECT(nint2::LoadElf64(broken,memory).error==ElfError::UnsupportedFormat);
    }

    // Harden parsers: 512 deterministic fuzz mutations and all short headers.
    std::uint32_t rng=0x45ac9911;
    unsigned rejected=0;
    for (int n=0;n<512;++n) {
        rng=rng*1664525U+1013904223U;
        auto fuzz=bytes;
        const auto at=static_cast<std::size_t>(rng%fuzz.size());
        fuzz[at]^=static_cast<std::uint8_t>(1U<<((rng>>20)&7));
        GuestMemory memory(0x800000);
        EXPECT(memory.Map(0x700000,4096,Access::Read|Access::Write));
        EXPECT(memory.Write64(0x700008,0xa55af00dULL));
        auto result=nint2::LoadElf64(fuzz,memory);
        if (result.error!=ElfError::None) ++rejected;
        EXPECT(memory.Read64(0x700008)==0xa55af00dULL);
    }
    EXPECT(rejected>=3);
    for (std::size_t n=0;n<64;++n) {
        GuestMemory memory(0x800000);
        EXPECT(nint2::LoadElf64({bytes.data(),n},memory).error!=ElfError::None);
    }
    std::cout << "Original AArch64 static PIE + RELATIVE relocations: "
                 "execution, three load bases, 21 negative paths and fuzz passed\n";
    return EXIT_SUCCESS;
}
