#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "nint2/elf.hpp"

#define CHECK(test) do { if (!(test)) { std::cerr << "FAILED " #test \
    << " line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {
constexpr std::uint64_t kPieBase=0x200000;
constexpr std::uint64_t kBias=kPieBase-0x10000;
constexpr std::uint64_t kRelaVa=0x20100;
constexpr std::uint64_t kTextVa=0x10200;
constexpr std::uint64_t kImageCodeOff=0x200;
constexpr std::uint64_t kRelOffset=0x1100;
void Put(std::vector<std::uint8_t>& data, std::uint64_t offset,
         std::uint64_t number, unsigned count) {
    for(unsigned i=0;i<count;++i)
        data.at(static_cast<std::size_t>(offset+i))=static_cast<std::uint8_t>(number>>(8*i));
}
[[nodiscard]] std::vector<std::uint8_t> Build(std::uint64_t count, bool reversed=false) {
    // Three independent original ELF program headers: RX PT_LOAD, RW PT_LOAD,
    // and a four-entry PT_DYNAMIC; every RELA target is writable guest data.
    const std::uint64_t data_va=kRelaVa+count*24;
    const std::uint64_t data_file=kRelOffset+count*24;
    const std::uint64_t file_size=0x100+count*32;
    std::vector<std::uint8_t> b(static_cast<std::size_t>(0x1000+file_size),0);
    b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';b[4]=2;b[5]=1;b[6]=1;
    Put(b,16,3,2);        // ET_DYN
    Put(b,18,183,2);      // EM_AARCH64
    Put(b,20,1,4);
    Put(b,24,kTextVa,8);
    Put(b,32,64,8);
    Put(b,52,64,2);
    Put(b,54,56,2);
    Put(b,56,3,2);
    Put(b,64,1,4);Put(b,68,5,4); // PT_LOAD RX
    Put(b,72,kImageCodeOff,8);Put(b,80,kTextVa,8);
    Put(b,96,4,8);Put(b,104,4,8);Put(b,112,0x100,8);
    Put(b,kImageCodeOff,0xd4200000,4); // original BRK #0
    Put(b,120,1,4);Put(b,124,6,4); // PT_LOAD RW
    Put(b,128,0x1000,8);Put(b,136,0x20000,8);
    Put(b,152,file_size,8);Put(b,160,file_size+4096,8);
    Put(b,168,0x1000,8);
    Put(b,176,2,4);  // PT_DYNAMIC in RW segment
    Put(b,184,0x1000,8);Put(b,192,0x20000,8);
    Put(b,208,64,8);Put(b,216,64,8);Put(b,224,8,8);
    Put(b,0x1000,7,8);Put(b,0x1008,kRelaVa,8); // DT_RELA
    Put(b,0x1010,8,8);Put(b,0x1018,count*24,8);// DT_RELASZ
    Put(b,0x1020,9,8);Put(b,0x1028,24,8);     // DT_RELAENT
    for(std::uint64_t i=0;i<count;++i) {
        const std::uint64_t target=reversed?count-i-1:i;
        const auto at=kRelOffset+i*24;
        Put(b,at,data_va+8*target,8);
        Put(b,at+8,1027,8);            // R_AARCH64_RELATIVE, symbol 0
        Put(b,at+16,0x20000+target,8); // unique addend per target
        Put(b,data_file+i*8,0xff,1);   // dirty initial payload
    }
    return b;
}
} // namespace

int main() {
    using nint2::Access;
    using nint2::ElfError;
    using nint2::GuestMemory;
    constexpr std::uint64_t entries=32000;
    const auto data_va=kRelaVa+entries*24;
    auto image=Build(entries);
    for(bool reversed : {false,true}) {
        if(reversed) image=Build(entries,true);
        GuestMemory mem(0x1000000);
        const auto result=nint2::LoadElf64(image,mem,kPieBase);
        CHECK(result.error==ElfError::None);
        CHECK(result.entry==kBias+kTextVa);
        CHECK(result.load_bias==kBias);
        CHECK(mem.Fetch32(result.entry)==0xd4200000U);
        CHECK(!mem.Write8(result.entry,0));
        CHECK(mem.Read64(kBias+data_va)==kBias+0x20000);
        CHECK(mem.Read64(kBias+data_va+8*(entries-1))==kBias+0x20000+entries-1);
        std::uint32_t seed=0x2f88e9c1;
        for(unsigned i=0;i<512;++i) {
            seed=seed*1664525+1013904223;
            const auto slot=seed%entries;
            CHECK(mem.Read64(kBias+data_va+8*slot)==kBias+0x20000+slot);
        }
        CHECK(!mem.Fetch32(kBias+data_va));
    }
    // Last relocation duplicates the first, far apart in input; reject before
    // committing any memory mappings or altering an unrelated guest sentinel.
    {
        auto duplicate=Build(entries);
        Put(duplicate,kRelOffset+(entries-1)*24,data_va,8);
        GuestMemory mem(0x1000000);
        CHECK(mem.Map(0x800000,4096,Access::Read|Access::Write));
        CHECK(mem.Write64(0x800000,0x123456789abcdef0ULL));
        CHECK(nint2::LoadElf64(duplicate,mem,kPieBase).error==ElfError::InvalidRelocation);
        CHECK(mem.Read64(0x800000)==0x123456789abcdef0ULL);
        CHECK(!mem.Fetch32(kBias+kTextVa));
        CHECK(!mem.Read64(kBias+data_va));
    }
    // Hard 65,536-entry resource ceiling, even for a valid-shaped image.
    {
        auto oversized=Build(65537);
        GuestMemory mem(0x1000000);
        CHECK(nint2::LoadElf64(oversized,mem,kPieBase).error==ElfError::InvalidRelocation);
        CHECK(!mem.Fetch32(kBias+kTextVa));
    }
    std::cout << "AArch64 static PIE: 32,000 ordered/reversed RELATIVE relocations, "
                 "duplicate rejection, and 65,537-entry guard passed\n";
    return EXIT_SUCCESS;
}
