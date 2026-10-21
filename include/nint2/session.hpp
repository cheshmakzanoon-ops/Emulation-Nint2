#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "nint2/cpu.hpp"

namespace nint2 {

// NOT a Nintendo/Horizon/Linux ABI. This original protocol is exclusively for
// source-controlled guest fixtures, with no passthrough to host system calls.
enum class SessionStop : std::uint8_t {
    Exited,
    CpuException,
    UnsupportedSupervisorCall,
    StepLimit,
    HostResourceFailure
};
struct SessionResult {
    SessionStop stop{SessionStop::StepLimit};
    CpuStop cpu_stop{CpuStop::Running};
    std::uint64_t steps{};          // cumulative executed guest instructions
    std::uint64_t exit_code{};
    std::uint64_t fault_address{};
    std::uint64_t service_number{};
};

class SyntheticSession final {
public:
    static constexpr std::uint64_t kMaxWrite = 4096;
    static constexpr std::uint64_t kMaxOutput = 65536;

    SyntheticSession(GuestMemory& memory, std::uint64_t entry, std::uint64_t stack_top) noexcept;
    [[nodiscard]] SessionResult Run(std::uint64_t max_steps);
    [[nodiscard]] CpuContext& Context() noexcept { return cpu_.context; }
    [[nodiscard]] const CpuContext& Context() const noexcept { return cpu_.context; }
    [[nodiscard]] std::string_view Stdout() const noexcept { return stdout_; }
    [[nodiscard]] std::string_view Stderr() const noexcept { return stderr_; }

private:
    [[nodiscard]] std::uint64_t HandleWrite();
    [[nodiscard]] SessionResult Outcome(SessionStop stop, CpuStop cpu_stop,
                                        std::uint64_t fault = 0,
                                        std::uint64_t service = 0) const noexcept;
    GuestMemory& memory_;
    CpuInterpreter cpu_;
    std::string stdout_;
    std::string stderr_;
    std::uint64_t steps_{};
    std::uint64_t exit_code_{};
    bool exited_{};
};
} // namespace nint2
