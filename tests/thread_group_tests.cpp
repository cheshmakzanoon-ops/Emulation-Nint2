#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/thread_group.hpp"

#define CHECK(test) do { if (!(test)) { std::cerr << "FAILED " #test \
    << " at line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)
namespace {
// Independently assembled original AArch64 loop (original_thread_counter.s).
constexpr std::array<std::uint32_t,7> kIncrementGuest{
    0x885ffc02,0x11000442,0x8803fc02,0x35ffffa3,
    0xf1000421,0x54ffff61,0xd4200000
};
bool Prepare(nint2::GuestMemory& mem){
    using nint2::Access;
    if(!mem.Map(0x1000,4096,Access::Read|Access::Write) ||
       !mem.Map(0x3000,4096,Access::Read|Access::Write)) return false;
    for(unsigned i=0;i<kIncrementGuest.size();++i)
        if(!mem.Write32(0x1000+4*i,kIncrementGuest[i]))return false;
    return mem.Protect(0x1000,4096,Access::Read|Access::Execute) &&
           mem.Write32(0x3000,0);
}
struct Replay{std::uint64_t steps;std::uint32_t counter;};
Replay Run(unsigned quantum,bool split){
    nint2::GuestMemory mem(0x10000);
    if(!Prepare(mem))return {0,0};
    nint2::SyntheticThreadGroup group(mem);
    for(unsigned i=0;i<4;++i){
        nint2::CpuContext start{};
        start.pc=0x1000;
        start.sp=0x4000-16*i;
        start.x[0]=0x3000;
        start.x[1]=200;
        if(group.Spawn(start)!=i)return {0,0};
    }
    if(split){
        for(unsigned i=0;i<17;++i){
            auto partial=group.Run(1,quantum);
            if(partial.stop!=nint2::ThreadGroupStop::InstructionBudget ||
               partial.executed!=1)return {0,0};
        }
    }
    auto done=group.Run(1000000,quantum);
    if(done.stop!=nint2::ThreadGroupStop::Completed ||
       done.runnable!=0 || done.faulted!=0 || done.halted!=4)return {0,0};
    for(unsigned i=0;i<4;++i){
        auto thread=group.Inspect(i);
        if(!thread || thread->state!=nint2::ThreadState::Halted ||
           thread->context.pc!=0x1018 || thread->context.x[1]!=0 ||
           thread->last_result.stop!=nint2::CpuStop::Breakpoint ||
           thread->executed<1201)return {0,0};
    }
    if(group.Run(10,quantum).executed!=0)return {0,0};
    return {group.TotalSteps(),mem.Read32(0x3000).value_or(0)};
}
} // namespace
int main(){
    using namespace nint2;
    for(unsigned quantum : {1U,2U,3U,5U,8U,32U,128U}) {
        const auto first=Run(quantum,false);
        const auto second=Run(quantum,false);
        if(first.counter!=800 || first.steps<4804 ||
           first.counter!=second.counter || first.steps!=second.steps){
            std::cerr << "replay mismatch quantum=" << quantum << " count="
                      << first.counter << " steps=" << first.steps << '\n';
            return EXIT_FAILURE;
        }
        const auto split=Run(quantum,true);
        const auto split_again=Run(quantum,true);
        CHECK(split.counter==800 && split.steps==split_again.steps);
    }
    GuestMemory mem(0x10000);
    CHECK(Prepare(mem));
    SyntheticThreadGroup empty(mem);
    CHECK(empty.Run(10,1).stop==ThreadGroupStop::NoThreads);
    CHECK(empty.Run(10,0).stop==ThreadGroupStop::InvalidQuantum);
    CpuContext start{};
    start.pc=0x1000;start.x[0]=0x3000;start.x[1]=1;
    CHECK(empty.Spawn(start)==0);
    CHECK(empty.Inspect(0).has_value() && !empty.Inspect(1));
    CHECK(empty.Run(100,0).stop==ThreadGroupStop::InvalidQuantum);
    CHECK(empty.Run(100,4097).stop==ThreadGroupStop::InvalidQuantum);
    CHECK(empty.TotalSteps()==0);
    CHECK(empty.Run(0,1).stop==ThreadGroupStop::InstructionBudget);
    CHECK(empty.TotalSteps()==0);
    CHECK(empty.Run(100,1).stop==ThreadGroupStop::Completed);
    CHECK(mem.Read32(0x3000)==1);
    CHECK(empty.Run(100,1).stop==ThreadGroupStop::Completed);
    CHECK(empty.TotalSteps()==7);
    CHECK(empty.Inspect(0)->executed==7);
    // Thread slots are capped; a halted context cannot restart.
    for(unsigned i=1;i<SyntheticThreadGroup::kMaxThreads;++i)
        CHECK(empty.Spawn(start)==i);
    CHECK(!empty.Spawn(start));
    CHECK(empty.Count()==SyntheticThreadGroup::kMaxThreads);
    CHECK(empty.Inspect(0)->state==ThreadState::Halted);
    // Faults do not suppress other runnable contexts.
    GuestMemory fault_mem(0x10000);
    CHECK(Prepare(fault_mem));
    CHECK(fault_mem.Map(0x2000,4096,Access::Read|Access::Write));
    CHECK(fault_mem.Write32(0x2000,0xffffffffU));
    CHECK(fault_mem.Protect(0x2000,4096,Access::Read|Access::Execute));
    SyntheticThreadGroup faults(fault_mem);
    CHECK(faults.Spawn(start)==0);
    CpuContext bad{};bad.pc=0x2000;
    CHECK(faults.Spawn(bad)==1);
    bad.pc=0x9000;
    CHECK(faults.Spawn(bad)==2);
    const auto failure=faults.Run(100,1);
    CHECK(failure.stop==ThreadGroupStop::ThreadFailure);
    CHECK(failure.halted==1 && failure.faulted==2 && failure.runnable==0);
    CHECK(fault_mem.Read32(0x3000)==1);
    CHECK(faults.Inspect(1)->last_result.stop==CpuStop::UndefinedInstruction);
    CHECK(faults.Inspect(2)->last_result.stop==CpuStop::InstructionFault);
    CHECK(faults.Run(100,1).executed==0);
    std::cout << "Synthetic shared-address-space AArch64 threads: 800 atomic "
                 "increments, seven scheduling quanta, replay and fault isolation passed\n";
    return EXIT_SUCCESS;
}
