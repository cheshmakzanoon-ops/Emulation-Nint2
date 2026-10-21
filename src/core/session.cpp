#include "nint2/session.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <new>
#include <stdexcept>
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

kernel::Result SyntheticSession::GrantQueueTo(std::uint32_t handle,
                                               SyntheticSession& recipient,
                                               kernel::Right rights) const {
    return handles_.GrantTo(handle, recipient.handles_, rights);
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

namespace {
[[nodiscard]] std::uint64_t QueueError(kernel::Error code) noexcept {
    switch (code) {
    case kernel::Error::None: return 0;
    case kernel::Error::InvalidHandle: return Error(9); // EBADF
    case kernel::Error::PermissionDenied: return Error(13); // EACCES
    case kernel::Error::InvalidArgument: return Error(22); // EINVAL
    case kernel::Error::Exhausted: return Error(24); // EMFILE
    case kernel::Error::Empty: return Error(11); // EAGAIN
    case kernel::Error::Full: return Error(11); // EAGAIN
    case kernel::Error::BufferTooSmall: return Error(90); // EMSGSIZE
    case kernel::Error::OutOfMemory: return Error(12); // ENOMEM
    }
    return Error(22);
}
[[nodiscard]] bool DecodeHandle(std::uint64_t raw, std::uint32_t& handle) noexcept {
    if (raw == 0 || raw > UINT32_MAX) return false;
    handle = static_cast<std::uint32_t>(raw);
    return true;
}
} // namespace

std::uint64_t SyntheticSession::HandleQueueCreate() {
    const auto capacity = cpu_.context.x[0];
    if (capacity == 0 || capacity > kernel::MessageQueue::kMaxMessages) return Error(22);
    const auto result = handles_.CreateQueue(static_cast<std::size_t>(capacity));
    return result.Ok() ? result.value : QueueError(result.error);
}
std::uint64_t SyntheticSession::HandleQueueSend() {
    std::uint32_t handle{};
    if (!DecodeHandle(cpu_.context.x[0], handle)) return Error(9);
    if (auto check = handles_.Check(handle, kernel::Right::Send); check != kernel::Error::None)
        return QueueError(check);
    const auto count = cpu_.context.x[2];
    const auto addr = cpu_.context.x[1];
    if (count > kernel::MessageQueue::kMaxMessage) return Error(22);
    // Stage into bounded host-owned scratch; invalid guest data does not enqueue.
    std::array<std::uint8_t, kernel::MessageQueue::kMaxMessage> buffer{};
    for (std::uint64_t i = 0; i < count; ++i) {
        if (i > UINT64_MAX - addr) return Error(14);
        const auto byte = memory_.Read8(addr + i);
        if (!byte) return Error(14);
        buffer[static_cast<std::size_t>(i)] = *byte;
    }
    const auto queue = handles_.Get(handle, kernel::Right::Send);
    const auto result = queue->Send(std::span<const std::uint8_t>(buffer.data(),
                                       static_cast<std::size_t>(count)));
    return result.Ok() ? result.value : QueueError(result.error);
}
std::uint64_t SyntheticSession::HandleQueueReceive() {
    std::uint32_t handle{};
    if (!DecodeHandle(cpu_.context.x[0], handle)) return Error(9);
    if (auto check = handles_.Check(handle, kernel::Right::Receive); check != kernel::Error::None)
        return QueueError(check);
    const auto capacity = cpu_.context.x[2];
    const auto addr = cpu_.context.x[1];
    if (capacity > kernel::MessageQueue::kMaxMessage) return Error(22);
    std::array<std::uint8_t, kernel::MessageQueue::kMaxMessage> buffer{};
    const auto queue = handles_.Get(handle, kernel::Right::Receive);
    const auto peek = queue->Peek(std::span<std::uint8_t>(buffer.data(),
                                    static_cast<std::size_t>(capacity)));
    if (!peek.Ok()) return QueueError(peek.error); // preserves head on EAGAIN/EMSGSIZE
    // Atomic across page boundaries: no consume and no partial byte write on fault.
    if (!memory_.WriteBuffer(addr, std::span<const std::uint8_t>(buffer.data(),
                                                      static_cast<std::size_t>(peek.value))))
        return Error(14);
    const auto removed = queue->Consume();
    return removed.Ok() ? removed.value : QueueError(removed.error);
}
std::uint64_t SyntheticSession::HandleQueueClose() {
    std::uint32_t handle{};
    if (!DecodeHandle(cpu_.context.x[0], handle)) return Error(9);
    return QueueError(handles_.Close(handle).error);
}
std::uint64_t SyntheticSession::HandleQueueDuplicate() {
    std::uint32_t handle{};
    if (!DecodeHandle(cpu_.context.x[0], handle)) return Error(9);
    if (cpu_.context.x[1] > UINT8_MAX) return Error(13);
    const auto rights = static_cast<kernel::Right>(cpu_.context.x[1]);
    const auto result = handles_.Duplicate(handle, rights);
    return result.Ok() ? result.value : QueueError(result.error);
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
        } else if (service == 4) { // Create bounded in-process message queue
            cpu_.context.x[0] = HandleQueueCreate();
        } else if (service == 5) { // Enqueue message from guest memory
            cpu_.context.x[0] = HandleQueueSend();
        } else if (service == 6) { // Dequeue message atomically into guest memory
            cpu_.context.x[0] = HandleQueueReceive();
        } else if (service == 7) { // Close queue handle
            cpu_.context.x[0] = HandleQueueClose();
        } else if (service == 8) { // Duplicate queue handle with subset of rights
            cpu_.context.x[0] = HandleQueueDuplicate();
        } else {
            return Outcome(SessionStop::UnsupportedSupervisorCall, step.stop,
                           cpu_.context.pc, service);
        }
        cpu_.context.pc += 4; // SVC is a four-byte AArch64 instruction
    }
    return Outcome(SessionStop::StepLimit, CpuStop::StepLimit, cpu_.context.pc);
}
} // namespace nint2
