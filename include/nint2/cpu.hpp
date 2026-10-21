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
};

struct CpuResult {
    CpuStop stop{CpuStop::Running};
    std::uint64_t steps{};
    std::uint64_t fault_address{};
    std::uint32_t instruction{};
};

// Deliberately incomplete AArch64 reference executor for original ELF test programs.
// All unsupported instructions trap; no privilege, SIMD, atomics or host syscalls.
class CpuInterpreter final {
public:
    CpuContext context{};
    [[nodiscard]] CpuResult Step(GuestMemory& memory);
    [[nodiscard]] CpuResult Run(GuestMemory& memory, std::uint64_t max_steps);

private:
    [[nodiscard]] std::optional<CpuResult> StepExtended(GuestMemory& memory, std::uint32_t instruction);
    [[nodiscard]] std::uint64_t ReadReg(unsigned index, bool wide, bool use_sp = false) const;
    void WriteReg(unsigned index, std::uint64_t value, bool wide, bool use_sp = false);
    void SetArithmeticFlags(std::uint64_t a, std::uint64_t b, std::uint64_t result,
                            bool wide, bool subtraction);
    [[nodiscard]] bool Condition(unsigned cond) const;
};
} // namespace nint2
