#include "nint2/session.hpp"
#include <cstdint>
#include <new>
#include <string>

namespace nint2 {
namespace {
constexpr std::uint64_t Error(int error_number) noexcept {
    return static_cast<std::uint64_t>(-static_cast<std::int64_t>(error_number));
}
} // namespace

SyntheticSession::SyntheticSession(GuestMemory& memory, std::uint64_t entry,
                                   std::uint64_t stack_top) noexcept : memory_(memory) {
    cpu_.context.pc = entry;
    cpu_.context.sp = stack_top;
}

SessionResult SyntheticSession::Outcome(SessionStop stop, CpuStop cpu_stop,
                                        std::uint64_t fault,
                                        std::uint64_t service) const noexcept {
    return {stop, cpu_stop, steps_, exit_code_, fault, service};
}

std::uint64_t SyntheticSession::HandleWrite() {
    const auto fd = cpu_.context.x[0];
    const auto address = cpu_.context.x[1];
    const auto count = cpu_.context.x[2];
    if (fd != 1 && fd != 2) return Error(9); // EBADF
    if (count > kMaxWrite) return Error(22); // EINVAL
    if (count > kMaxOutput - stdout_.size() - stderr_.size()) return Error(28); // ENOSPC

    // All-or-nothing guest-buffer read: invalid pointers cannot produce partial output.
    std::string buffer;
    buffer.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        if (i > UINT64_MAX - address) return Error(14); // EFAULT / integer wrap
        const auto value = memory_.Read8(address + i);
        if (!value) return Error(14);
        buffer.push_back(static_cast<char>(*value));
    }
    (fd == 1 ? stdout_ : stderr_).append(buffer);
    return count;
}

SessionResult SyntheticSession::Run(std::uint64_t max_steps) {
    if (exited_) return Outcome(SessionStop::Exited, CpuStop::SupervisorCall);
    for (std::uint64_t i = 0; i < max_steps; ++i) {
        const auto step = cpu_.Step(memory_);
        ++steps_;
        if (step.stop == CpuStop::Running) continue;
        if (step.stop != CpuStop::SupervisorCall)
            return Outcome(SessionStop::CpuException, step.stop, step.fault_address);

        const auto service = cpu_.context.x[8];
        // Only SVC #0 enters our synthetic protocol. Unknown opcodes never
        // silently succeed or advance PC.
        if (((step.instruction >> 5) & 0xffffU) != 0)
            return Outcome(SessionStop::UnsupportedSupervisorCall, step.stop,
                           cpu_.context.pc, service);
        if (service == 2) { // Exit(code=x0)
            exited_ = true;
            exit_code_ = cpu_.context.x[0];
            return Outcome(SessionStop::Exited, step.stop);
        }
        if (service == 1) { // Write(fd=x0, guest_ptr=x1, length=x2)
            try {
                cpu_.context.x[0] = HandleWrite();
            } catch (const std::bad_alloc&) {
                return Outcome(SessionStop::HostResourceFailure, step.stop);
            } catch (const std::length_error&) {
                return Outcome(SessionStop::HostResourceFailure, step.stop);
            }
        } else if (service == 3) { // Deterministic instruction tick counter
            cpu_.context.x[0] = steps_;
        } else {
            return Outcome(SessionStop::UnsupportedSupervisorCall, step.stop,
                           cpu_.context.pc, service);
        }
        cpu_.context.pc += 4; // SVC is a four-byte AArch64 instruction
    }
    return Outcome(SessionStop::StepLimit, CpuStop::StepLimit, cpu_.context.pc);
}
} // namespace nint2
