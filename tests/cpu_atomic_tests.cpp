#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/cpu.hpp"

#define REQUIRE(ok) do { if (!(ok)) { std::cerr << "FAIL " << #ok \
 << " line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

int main() {
    using namespace nint2;
    // Independently assembled: tests/fixtures/original_atom.s (Clang AArch64).
    constexpr std::array<std::uint32_t,21> insns{
        0x085f7c20,0x085ffc62,0x08047cc5,0x0807fd28,
        0x485f7d6a,0x485ffdac,0x480e7e0f,0x4811fe72,
        0x885f7eb4,0x885ffef6,0x88187f59,0x881bffbc,
        0xc85f7c20,0xc85ffc62,0xc8047cc5,0xc807fd28,
        0xd5033f5f,0xd503305f,0xd5033bbf,0xd5033f9f,0xd5033fdf
    };
    GuestMemory memory(0x20000);
    REQUIRE(memory.Map(0x1000,4096,Access::Read|Access::Write));
    REQUIRE(memory.Map(0x3000,4096,Access::Read|Access::Write));
    REQUIRE(memory.Map(0x6000,4096,Access::Read|Access::Write));
    for(unsigned i=0;i<insns.size();++i)
        REQUIRE(memory.Write32(0x1000+4*i,insns[i]));
    REQUIRE(memory.Protect(0x1000,4096,Access::Read|Access::Execute));
    CpuInterpreter cpu;
    auto step=[&](unsigned i){cpu.context.pc=0x1000+4*i;return cpu.Step(memory);};
    struct Pair {unsigned load,store,base,dst,source,status,bytes;};
    constexpr std::array<Pair,8> pairs{{
        {0,2,1,0,5,4,1},{1,3,3,2,8,7,1},
        {4,6,11,10,15,14,2},{5,7,13,12,18,17,2},
        {8,10,21,20,25,24,4},{9,11,23,22,28,27,4},
        {12,14,1,0,5,4,8},{13,15,3,2,8,7,8}
    }};
    for(unsigned i=0;i<pairs.size();++i) {
        const auto& p=pairs[i];
        const auto address=0x3100U+16U*i;
        cpu.context.x[p.base]=address;
        // Some tests use X1/X3 for W and X; update corresponding store base.
        const unsigned store_base=(i%2==0? (i==0?6:i==2?16:i==4?26:6) :
                                   (i==1?9:i==3?19:i==5?29:9));
        cpu.context.x[store_base]=address;
        const auto initial=0x1234567890abcdefULL;
        const auto next=0xabba11223344eeffULL;
        REQUIRE(memory.Write64(address,initial));
        REQUIRE(step(p.load).stop==CpuStop::Running);
        const auto mask=p.bytes==8?UINT64_MAX:((1ULL<<(8*p.bytes))-1);
        REQUIRE(cpu.context.x[p.dst]==(initial&mask));
        cpu.context.x[p.source]=next;
        REQUIRE(step(p.store).stop==CpuStop::Running);
        REQUIRE(cpu.context.x[p.status]==0);
        REQUIRE((memory.Read64(address).value()&mask)==(next&mask));
        // A second STXR without LDXR must fail, not write memory.
        REQUIRE(step(p.store).stop==CpuStop::Running);
        REQUIRE(cpu.context.x[p.status]==1);
    }
    cpu.context.x[1]=cpu.context.x[6]=0x3100;
    cpu.context.x[5]=0xbeef;
    // CLREX clears reservation and memory barriers advance PC.
    for(unsigned clrex : {16U,17U}) {
        REQUIRE(step(12).stop==CpuStop::Running);
        REQUIRE(step(clrex).stop==CpuStop::Running);
        REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==1);
    }
    for(unsigned barrier : {18U,19U,20U}) {
        REQUIRE(step(barrier).stop==CpuStop::Running);
        REQUIRE(cpu.context.pc==0x1000+4*(barrier+1));
    }
    // Unrelated host writes preserve reservation; same frame writes revoke.
    REQUIRE(step(12).stop==CpuStop::Running);
    REQUIRE(memory.Write8(0x6000,7));
    REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==0);
    REQUIRE(step(12).stop==CpuStop::Running);
    REQUIRE(memory.Write8(0x317f,6));
    REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==1);
    REQUIRE(memory.Alias(0x5000,0x3000,4096,Access::Read|Access::Write));
    REQUIRE(step(12).stop==CpuStop::Running);
    REQUIRE(memory.Write8(0x5100,3));
    REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==1);
    // Two CPU contexts may load the same value but only one first store wins.
    CpuInterpreter peer;
    peer.context.x[1]=peer.context.x[6]=0x3100;
    peer.context.x[5]=0xaaaaaaaabbbbbbbbULL;
    cpu.context.x[5]=0x1111111122222222ULL;
    REQUIRE(step(12).stop==CpuStop::Running);
    peer.context.pc=0x1030;
    REQUIRE(peer.Step(memory).stop==CpuStop::Running);
    REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==0);
    peer.context.pc=0x1038;
    REQUIRE(peer.Step(memory).stop==CpuStop::Running && peer.context.x[4]==1);
    peer.context.pc=0x1030;
    REQUIRE(peer.Step(memory).stop==CpuStop::Running);
    peer.context.pc=0x1038;
    REQUIRE(peer.Step(memory).stop==CpuStop::Running && peer.context.x[4]==0);
    REQUIRE(memory.Read64(0x3100)==peer.context.x[5]);
    // Unmapped and unaligned guest addresses fault without advancing PC.
    cpu.context.x[1]=0x9000;
    REQUIRE(step(12).stop==CpuStop::DataFault && cpu.context.pc==0x1030);
    cpu.context.x[1]=0x3101;
    REQUIRE(step(12).stop==CpuStop::DataFault && cpu.context.pc==0x1030);
    cpu.context.x[1]=0x3100;
    REQUIRE(step(12).stop==CpuStop::Running);
    REQUIRE(memory.Protect(0x3000,4096,Access::Read));
    REQUIRE(step(14).stop==CpuStop::DataFault && cpu.context.pc==0x1038);
    REQUIRE(memory.Protect(0x3000,4096,Access::Read|Access::Write));
    // Replaced mappings must have different physical-frame identities.
    REQUIRE(step(12).stop==CpuStop::Running);
    REQUIRE(memory.Unmap(0x5000,4096));
    REQUIRE(memory.Unmap(0x3000,4096));
    REQUIRE(memory.Map(0x3000,4096,Access::Read|Access::Write));
    REQUIRE(step(14).stop==CpuStop::Running && cpu.context.x[4]==1);
    REQUIRE(memory.Read64(0x3100)==0);
    // Any load/store exclusive encoding we do not implement must trap.
    REQUIRE(memory.Map(0x2000,4096,Access::Read|Access::Write));
    REQUIRE(memory.Write32(0x2000,0xc85f7820)); // pair/inconsistent Rt2
    REQUIRE(memory.Write32(0x2004,0xc8407c20)); // reserved Rs on LDXR
    REQUIRE(memory.Protect(0x2000,4096,Access::Read|Access::Execute));
    for(auto pc : {0x2000ULL,0x2004ULL}) {
        cpu.context.pc=pc;
        REQUIRE(cpu.Step(memory).stop==CpuStop::UndefinedInstruction);
        REQUIRE(cpu.context.pc==pc);
    }
    std::cout << "AArch64 exclusive byte/halfword/word/doubleword, monitor, "
                 "alias and contention tests passed\n";
    return EXIT_SUCCESS;
}
