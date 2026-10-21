#include "nint2/thread_group.hpp"
#include <new>
#include <utility>

namespace nint2 {
std::optional<std::uint32_t> SyntheticThreadGroup::Spawn(const CpuContext& context) {
    if (threads_.size() >= kMaxThreads) return std::nullopt;
    try {
        Thread thread;
        thread.cpu.context = context;
        const auto id = static_cast<std::uint32_t>(threads_.size());
        threads_.push_back(std::move(thread));
        return id;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}
std::optional<ThreadReport> SyntheticThreadGroup::Inspect(std::uint32_t id) const {
    if (id >= threads_.size()) return std::nullopt;
    const auto& thread=threads_[id];
    return ThreadReport{id,thread.state,thread.cpu.context,thread.last,thread.steps};
}
ThreadGroupReport SyntheticThreadGroup::Summary(ThreadGroupStop stop,
                                                 std::uint64_t executed) const noexcept {
    ThreadGroupReport report{stop,executed};
    for (const auto& thread : threads_) {
        switch (thread.state) {
        case ThreadState::Runnable: ++report.runnable; break;
        case ThreadState::Halted: ++report.halted; break;
        case ThreadState::Faulted: ++report.faulted; break;
        }
    }
    return report;
}
ThreadGroupReport SyntheticThreadGroup::Run(std::uint64_t budget,
                                             std::uint32_t quantum) {
    if (quantum == 0 || quantum > kMaxQuantum)
        return Summary(ThreadGroupStop::InvalidQuantum,0);
    if (threads_.empty()) return Summary(ThreadGroupStop::NoThreads,0);
    std::uint64_t completed=0;
    while (completed < budget) {
        // Scanning at most 32 contexts cannot starve other runnable contexts.
        std::size_t index=next_index_;
        std::size_t scanned=0;
        while (scanned < threads_.size() &&
               threads_[index].state != ThreadState::Runnable) {
            index=(index+1)%threads_.size();
            ++scanned;
        }
        if (scanned == threads_.size()) break;
        next_index_=(index+1)%threads_.size();
        auto& thread=threads_[index];
        for (std::uint32_t i=0;i<quantum && completed<budget;++i) {
            const auto result=thread.cpu.Step(memory_);
            ++thread.steps;
            ++completed;
            ++total_steps_;
            if (result.stop==CpuStop::Running) continue;
            thread.last=result;
            thread.state=(result.stop==CpuStop::Breakpoint) ?
                         ThreadState::Halted : ThreadState::Faulted;
            // Only this halted/faulted context loses its exclusive monitor.
            thread.cpu.ClearExclusive();
            break;
        }
    }
    const auto snapshot=Summary(ThreadGroupStop::InstructionBudget,completed);
    if (snapshot.runnable==0)
        return Summary(snapshot.faulted ? ThreadGroupStop::ThreadFailure
                                       : ThreadGroupStop::Completed,completed);
    return snapshot;
}
} // namespace nint2
