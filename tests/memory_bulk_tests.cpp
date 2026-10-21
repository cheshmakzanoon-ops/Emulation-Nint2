#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "nint2/memory.hpp"
#define CHECK(test) do { if (!(test)) { std::cerr << "FAILED: " #test \
  << " at " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

int main() {
    using nint2::Access;
    using nint2::GuestMemory;
    constexpr std::uint64_t page=GuestMemory::kPageSize;
    // Large guest ELF/asset segments use the bulk path, not individual bytes.
    constexpr std::size_t pages=256;
    GuestMemory big(16ULL*1024*1024);
    CHECK(big.Map(0x10000,pages*page,Access::Read|Access::Write));
    std::vector<std::uint8_t> data(pages*page+1);
    std::uint32_t seed=0x72657175;
    for(auto& b : data) {
        seed=seed*1664525+1013904223;
        b=static_cast<std::uint8_t>(seed>>17);
    }
    CHECK(big.WriteBuffer(0x10000,std::span<const std::uint8_t>(data.data(),pages*page)));
    for(std::size_t i=0;i<pages*page;i+=97)
        CHECK(big.Read8(0x10000+i).value()==data[i]);
    for(std::size_t i=0;i<pages;++i) {
        CHECK(big.Read8(0x10000+i*page).value()==data[i*page]);
        CHECK(big.Read8(0x10000+(i+1)*page-1).value()==data[(i+1)*page-1]);
    }
    CHECK(!big.WriteBuffer(0x10000,std::span<const std::uint8_t>(data)));
    CHECK(big.Read8(0x10000)==data[0]);
    CHECK(big.Read8(0x10000+pages*page-1)==data[pages*page-1]);
    CHECK(big.Map(0x200000,page,Access::Read|Access::Write));
    CHECK(big.Read64(0x2007f8)==0);
    // Writes crossing an unmapped or read-only page reject atomically.
    std::vector<std::uint8_t> pattern(32,0xa5);
    GuestMemory sparse(0x10000);
    CHECK(sparse.Map(0x2000,page,Access::Read|Access::Write));
    CHECK(!sparse.WriteBuffer(0x2ff0,pattern));
    CHECK(sparse.Read64(0x2ff0)==0);
    CHECK(sparse.Map(0x3000,page,Access::Read));
    CHECK(!sparse.WriteBuffer(0x2ff0,pattern));
    CHECK(sparse.Read64(0x2ff0)==0);
    CHECK(sparse.Protect(0x3000,page,Access::Read|Access::Write));
    CHECK(sparse.WriteBuffer(0x2ff0,pattern));
    for(unsigned i=0;i<32;++i) CHECK(sparse.Read8(0x2ff0+i)==0xa5);
    CHECK(sparse.Read64(0x2fe8)==0);
    CHECK(sparse.Read64(0x3010)==0);
    // Bulk write through a VA alias invalidates physical frame reservations.
    CHECK(sparse.Alias(0x6000,0x2000,page,Access::Read|Access::Write));
    auto exclusive=sparse.LoadExclusive(0x2ff0,8);
    CHECK(exclusive.has_value());
    std::vector<std::uint8_t> changed(24,0x7c);
    CHECK(sparse.WriteBuffer(0x6ff0,std::span<const std::uint8_t>(changed.data(),16)));
    CHECK(sparse.Read64(0x2ff0)==0x7c7c7c7c7c7c7c7cULL);
    CHECK(sparse.StoreExclusive(0x2ff0,8,0x1234,exclusive->frame_id,exclusive->generation)
          ==GuestMemory::ExclusiveStore::ReservationLost);
    // Two consecutive VAs can refer to one physical backing frame.
    CHECK(sparse.Alias(0x7000,0x2000,page,Access::Read|Access::Write));
    std::vector<std::uint8_t> both(page,0x17);
    std::fill(both.begin()+16,both.end(),0x25);
    CHECK(sparse.WriteBuffer(0x6ff0,both));
    CHECK(sparse.Read8(0x2ff0)==0x17);
    CHECK(sparse.Read8(0x2000)==0x25);
    CHECK(sparse.Read8(0x2010)==0x25);
    CHECK(sparse.Alias(0x8000,0x2000,page,Access::Read));
    const auto before=sparse.Read64(0x2000);
    CHECK(!sparse.WriteBuffer(0x8000,changed));
    CHECK(sparse.Read64(0x2000)==before);
    // Repeated small unaligned writes preserve exclusive generations.
    for(unsigned offset=1;offset<64;++offset) {
        const auto stamp=sparse.LoadExclusive(0x2080,8);
        CHECK(stamp.has_value());
        const auto len=static_cast<std::size_t>(offset*3);
        CHECK(sparse.WriteBuffer(0x2800+offset,
             std::span<const std::uint8_t>(data.data(),len)));
        CHECK(sparse.StoreExclusive(0x2080,8,7,stamp->frame_id,stamp->generation)
              ==GuestMemory::ExclusiveStore::ReservationLost);
        CHECK(sparse.Read8(0x2800+offset)==data[0]);
        CHECK(sparse.Read8(0x2800+offset+len-1)==data[len-1]);
    }
    CHECK(sparse.WriteBuffer(0xffff,{}));
    CHECK(!sparse.WriteBuffer(UINT64_MAX,changed));
    std::cout << "Guest memory: 1 MiB page-chunk writes, aliases, rollback, "
                 "monitor generations and sparse zero-fill passed\n";
    return EXIT_SUCCESS;
}
