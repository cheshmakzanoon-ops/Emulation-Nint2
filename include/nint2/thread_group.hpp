#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "nint2/cpu.hpp"

namespace nint2 {
// Deterministic synthetic thread experiment: original AArch64 CPU contexts
// in ONE guest address space, not Nintendo/Horizon OS threads.
enum class ThreadState : std::uint8_t { Runnable, Halted, Faulted };
enum class ThreadGroupStop : std::uint8_t {
    Completed, ThreadFailure, InstructionBudget, NoThreads, InvalidQuantum
};
struct ThreadReport {
    std::uint32_t id{};
    ThreadState state{ThreadState::Runnable};
    CpuContext context{};
    CpuResult last_result{};
    std::uint64_t executed{};
};
struct ThreadGroupReport {
    ThreadGroupStop stop{ThreadGroupStop::NoThreads};
    std::uint64_t executed{}; // instructions executed by THIS Run call
    std::size_t runnable{}, halted{}, faulted{};
};
class SyntheticThreadGroup final {
public:
    static constexpr std::size_t kMaxThreads = 32;
    static constexpr std::uint32_t kMaxQuantum = 4096;
    explicit SyntheticThreadGroup(GuestMemory& memory) noexcept : memory_(memory) {}
    // Thread IDs are monotonically increasing indices and cannot be reused.
    // Each context has a fresh CPU-exclusive monitor and private registers.
    [[nodiscard]] std::optional<std::uint32_t> Spawn(const CpuContext& context);
    [[nodiscard]] std::optional<ThreadReport> Inspect(std::uint32_t id) const;
    [[nodiscard]] std::size_t Count() const noexcept { return threads_.size(); }
    [[nodiscard]] std::uint64_t TotalSteps() const noexcept { return total_steps_; }
    // Round robin preserves the next context across split budgets. Breakpoint
    // is a successful halt; SVC, bad instructions and faults are failures.
    [[nodiscard]] ThreadGroupReport Run(std::uint64_t budget, std::uint32_t quantum);
private:
    struct Thread {
        CpuInterpreter cpu{};
        ThreadState state{ThreadState::Runnable};
        CpuResult last{};
        std::uint64_t steps{};
    };
    [[nodiscard]] ThreadGroupReport Summary(ThreadGroupStop stop,
                                           std::uint64_t executed) const noexcept;
    GuestMemory& memory_;
    std::vector<Thread> threads_{};
    std::size_t next_index_{};
    std::uint64_t total_steps_{};
};
} // namespace nint2
