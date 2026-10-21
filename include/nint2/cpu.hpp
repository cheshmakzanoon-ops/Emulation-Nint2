#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include "nint2/memory.hpp"

namespace nint2 {

enum class CpuStop {
    Running,
    Breakpoint,
    SupervisorCall,
    InstructionFault,
    DataFault,
    UndefinedInstruction,
    StepLimit
};

struct CpuContext {
    std::array<std::uint64_t, 31> x{};
    std::uint64_t sp{};
    std::uint64_t pc{};
    // NZCV in bits 3:0 (N=8, Z=4, C=2, V=1).
    std::uint8_t nzcv{};
    // Per-guest EL0 thread pointer, never a host-process pointer.
    std::uint64_t tpidr_el0{};
};

struct CpuResult {
    CpuStop stop{CpuStop::Running};
    std::uint64_t steps{};
    std::uint64_t fault_address{};
    std::uint32_t instruction{};
};

// Deliberately incomplete AArch64 reference executor for original ELF test programs.
// All unsupported instructions trap; no privilege, SIMD or host syscalls.
class CpuInterpreter final {
public:
    CpuContext context{};
    [[nodiscard]] CpuResult Step(GuestMemory& memory);
    [[nodiscard]] CpuResult Run(GuestMemory& memory, std::uint64_t max_steps);
    // Architectural CLREX/guest context reset. The monitor is not copied into
    // a separate guest context, and is retained across Run() quantum slices.
    void ClearExclusive() noexcept { exclusive_.valid = false; }

private:
    [[nodiscard]] std::optional<CpuResult> StepExtended(GuestMemory& memory, std::uint32_t instruction);
    [[nodiscard]] std::optional<CpuResult> StepAtomic(GuestMemory& memory, std::uint32_t instruction);
    [[nodiscard]] std::optional<CpuResult> StepLse(GuestMemory& memory, std::uint32_t instruction);
    [[nodiscard]] std::optional<CpuResult> StepSystem(std::uint32_t instruction);
    [[nodiscard]] std::optional<CpuResult> StepCrc(std::uint32_t instruction);
    [[nodiscard]] std::optional<CpuResult> StepPair(GuestMemory& memory, std::uint32_t instruction);
    struct ExclusiveReservation {
        bool valid{};
        const GuestMemory* space{};
        std::uint64_t address{};
        unsigned bytes{};
        std::uint64_t frame_id{}, generation{};
    } exclusive_;
    [[nodiscard]] std::uint64_t ReadReg(unsigned index, bool wide, bool use_sp = false) const;
    void WriteReg(unsigned index, std::uint64_t value, bool wide, bool use_sp = false);
    void SetArithmeticFlags(std::uint64_t a, std::uint64_t b, std::uint64_t result,
                            bool wide, bool subtraction);
    [[nodiscard]] bool Condition(unsigned cond) const;
};
} // namespace nint2
