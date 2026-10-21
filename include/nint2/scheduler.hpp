#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "nint2/session.hpp"

namespace nint2 {

// A deterministic host-only scheduler for independently owned SyntheticSession
// instances. It does not represent native Horizon processes or scheduling.
enum class SchedulerStop : std::uint8_t {
    Completed, BudgetExhausted, GuestFailed, InvalidConfiguration, Deadlock
};
struct SchedulerResult {
    SchedulerStop stop{SchedulerStop::InvalidConfiguration};
    std::uint64_t executed_steps{};  // cumulative across Run calls
    std::size_t process_count{};
    std::size_t exited{};
    std::size_t failed{};
};

// The caller owns all sessions, memory spaces and their lifetimes. Each Run()
// accepts its own aggregate instruction budget and distributes bounded slices
// round-robin. A fault terminates only the faulting process; no host threads.
class SyntheticScheduler final {
public:
    static constexpr std::size_t kMaxProcesses = 16;
    [[nodiscard]] bool Add(SyntheticSession& session) noexcept;
    [[nodiscard]] SchedulerResult Run(std::uint64_t max_steps,
                                       std::uint64_t quantum = 1);
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
private:
    [[nodiscard]] SchedulerResult Summary(SchedulerStop stop) const noexcept;
    std::array<SyntheticSession*, kMaxProcesses> sessions_{};
    std::array<bool, kMaxProcesses> finished_{};
    std::array<bool, kMaxProcesses> blocked_{};
    std::array<SessionStop, kMaxProcesses> outcomes_{};
    std::size_t size_{};
    std::size_t next_{};
    std::uint64_t executed_{};
};
} // namespace nint2
