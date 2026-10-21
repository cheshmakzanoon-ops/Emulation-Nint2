#include "nint2/scheduler.hpp"
#include <algorithm>

namespace nint2 {

bool SyntheticScheduler::Add(SyntheticSession& session) noexcept {
    if (size_ >= kMaxProcesses) return false;
    for (std::size_t i = 0; i < size_; ++i)
        if (sessions_[i] == &session) return false;
    sessions_[size_] = &session;
    finished_[size_] = false;
    outcomes_[size_] = SessionStop::StepLimit;
    ++size_;
    return true;
}

SchedulerResult SyntheticScheduler::Summary(SchedulerStop stop) const noexcept {
    SchedulerResult result{stop, executed_, size_, 0, 0};
    for (std::size_t i = 0; i < size_; ++i) {
        if (!finished_[i]) continue;
        if (outcomes_[i] == SessionStop::Exited) ++result.exited;
        else ++result.failed;
    }
    return result;
}

SchedulerResult SyntheticScheduler::Run(std::uint64_t max_steps,
                                        std::uint64_t quantum) {
    if (size_ == 0 || quantum == 0)
        return Summary(SchedulerStop::InvalidConfiguration);

    std::uint64_t spent = 0;
    while (true) {
        std::size_t unfinished = 0;
        for (std::size_t i = 0; i < size_; ++i)
            if (!finished_[i]) ++unfinished;
        if (unfinished == 0) {
            const auto summary = Summary(SchedulerStop::Completed);
            return Summary(summary.failed == 0 ? SchedulerStop::Completed
                                               : SchedulerStop::GuestFailed);
        }
        if (spent == max_steps) return Summary(SchedulerStop::BudgetExhausted);

        std::size_t index = next_;
        for (std::size_t scan = 0; scan < size_ && finished_[index]; ++scan)
            index = (index + 1) % size_;
        const auto before = sessions_[index]->TotalSteps();
        const auto slice = std::min(quantum, max_steps - spent);
        const auto outcome = sessions_[index]->Run(slice);
        const auto taken = outcome.steps - before;
        spent += taken;
        executed_ += taken;
        if (outcome.stop != SessionStop::StepLimit) {
            finished_[index] = true;
            outcomes_[index] = outcome.stop;
        }
        next_ = (index + 1) % size_;
    }
}
} // namespace nint2
