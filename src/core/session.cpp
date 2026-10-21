#include "nint2/session.hpp"
#include <array>
#include <algorithm>
#include <bit>
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

kernel::Result SyntheticSession::ImportQueue(const kernel::HandleTable& owner,
                                             std::uint32_t handle,
                                             kernel::Right rights) {
    return owner.GrantTo(handle, handles_, rights);
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
    case kernel::Error::PeerClosed: return Error(32); // EPIPE
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

bool SyntheticSession::CanResume() const noexcept {
    if (!waiting_) return true;
    const auto status = handles_.Status(wait_handle_, wait_direction_);
    return status.error != kernel::Error::Empty && status.error != kernel::Error::Full;
}


namespace {
// Conversion for original, explicitly synthetic read-only resource services.
[[nodiscard]] std::uint64_t AssetError(assets::Error error) noexcept {
    switch (error) {
    case assets::Error::None: return 0;
    case assets::Error::InvalidPath:
    case assets::Error::InvalidArgument:
    case assets::Error::TooLarge: return Error(22); // EINVAL
    case assets::Error::Duplicate: return Error(17); // EEXIST
    case assets::Error::NotFound: return Error(2); // ENOENT
    case assets::Error::TooManyAssets:
    case assets::Error::TooManyHandles: return Error(24); // EMFILE
    case assets::Error::NoSpace: return Error(28); // ENOSPC
    case assets::Error::InvalidHandle: return Error(9); // EBADF
    case assets::Error::OutOfMemory: return Error(12); // ENOMEM
    case assets::Error::Sealed: return Error(30); // EROFS
    }
    return Error(22);
}
[[nodiscard]] std::uint64_t GuestPath(const GuestMemory& memory, std::uint64_t addr,
    std::uint64_t count, std::array<char, assets::AssetStore::kMaxPath>& out) noexcept {
    if (count == 0 || count > out.size()) return Error(22);
    for (std::uint64_t i = 0; i < count; ++i) {
        if (i > UINT64_MAX - addr) return Error(14);
        const auto byte = memory.Read8(addr + i);
        if (!byte) return Error(14);
        out[static_cast<std::size_t>(i)] = static_cast<char>(*byte);
    }
    return 0;
}
void Store64(std::span<std::uint8_t> out, std::size_t at, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i) out[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
} // namespace

std::uint64_t SyntheticSession::HandleAssetOpen() {
    if (cpu_.context.x[2] != 0) return Error(22); // read-only is the only mode
    std::array<char, assets::AssetStore::kMaxPath> path{};
    if (const auto status = GuestPath(memory_, cpu_.context.x[0], cpu_.context.x[1], path))
        return status;
    const auto name = std::string_view(path.data(), static_cast<std::size_t>(cpu_.context.x[1]));
    const auto result = assets_.Open(name);
    return result.Ok() ? result.value : AssetError(result.error);
}

std::uint64_t SyntheticSession::HandleAssetRead(bool positioned) {
    std::uint32_t fd{};
    if (!DecodeHandle(cpu_.context.x[0], fd)) return Error(9);
    const auto count = cpu_.context.x[2];
    if (count > assets::AssetStore::kMaxRead) return Error(22);
    std::array<std::uint8_t, assets::AssetStore::kMaxRead> bytes{};
    const auto destination = std::span<std::uint8_t>(bytes.data(), static_cast<std::size_t>(count));
    const auto result = positioned ?
        assets_.PeekAt(fd, cpu_.context.x[3], destination) :
        assets_.Peek(fd, destination);
    if (!result.Ok()) return AssetError(result.error);
    if (!memory_.WriteBuffer(cpu_.context.x[1], destination.first(
                                 static_cast<std::size_t>(result.value))))
        return Error(14); // retain offset on guest-buffer failure
    if (!positioned) {
        const auto committed = assets_.Consume(fd, static_cast<std::size_t>(result.value));
        if (!committed.Ok()) return AssetError(committed.error);
    }
    return result.value;
}

std::uint64_t SyntheticSession::HandleAssetSeek() {
    std::uint32_t fd{};
    if (!DecodeHandle(cpu_.context.x[0], fd)) return Error(9);
    if (cpu_.context.x[2] > 2) return Error(22);
    const auto delta = std::bit_cast<std::int64_t>(cpu_.context.x[1]);
    const auto result = assets_.Seek(fd, delta, static_cast<unsigned>(cpu_.context.x[2]));
    return result.Ok() ? result.value : AssetError(result.error);
}

std::uint64_t SyntheticSession::HandleAssetClose() {
    std::uint32_t fd{};
    if (!DecodeHandle(cpu_.context.x[0], fd)) return Error(9);
    return AssetError(assets_.Close(fd).error);
}

std::uint64_t SyntheticSession::HandleAssetStat() {
    std::array<char, assets::AssetStore::kMaxPath> path{};
    if (const auto status = GuestPath(memory_, cpu_.context.x[0], cpu_.context.x[1], path))
        return status;
    const auto name = std::string_view(path.data(), static_cast<std::size_t>(cpu_.context.x[1]));
    assets::Info info{};
    if (const auto err = assets_.Stat(name, info); err != assets::Error::None)
        return AssetError(err);
    std::array<std::uint8_t, 16> out{};
    Store64(out, 0, info.size);
    Store64(out, 8, info.digest);
    return memory_.WriteBuffer(cpu_.context.x[2], out) ? 0 : Error(14);
}

std::uint64_t SyntheticSession::HandleAssetFstat() {
    std::uint32_t fd{};
    if (!DecodeHandle(cpu_.context.x[0], fd)) return Error(9);
    assets::Info info{};
    if (const auto err = assets_.Fstat(fd, info); err != assets::Error::None)
        return AssetError(err);
    std::array<std::uint8_t, 24> out{};
    Store64(out, 0, info.size);
    Store64(out, 8, info.digest);
    Store64(out, 16, info.offset);
    return memory_.WriteBuffer(cpu_.context.x[1], out) ? 0 : Error(14);
}

std::uint64_t SyntheticSession::HandleAssetEnumerate() {
    // One all-or-nothing 152-byte guest write prevents a half-written record.
    constexpr std::size_t kRecordSize = 24 + assets::AssetStore::kMaxPath;
    if (cpu_.context.x[0] >= assets_.AssetCount()) return 0; // EOF enumeration
    if (cpu_.context.x[2] < kRecordSize) return Error(90); // EMSGSIZE
    std::string_view name;
    assets::Info info{};
    if (!assets_.Enumerate(static_cast<std::size_t>(cpu_.context.x[0]), name, info))
        return 0;
    std::array<std::uint8_t, kRecordSize> record{};
    Store64(record, 0, info.size);
    Store64(record, 8, info.digest);
    Store64(record, 16, name.size());
    std::copy(name.begin(), name.end(), record.begin() + 24);
    return memory_.WriteBuffer(cpu_.context.x[1], record) ? 1 : Error(14);
}

namespace {
[[nodiscard]] std::uint64_t VirtualError(vm::Error error) noexcept {
    switch (error) {
    case vm::Error::None: return 0;
    case vm::Error::InvalidArgument:
    case vm::Error::InvalidRegion: return Error(22); // EINVAL
    case vm::Error::AddressInUse: return Error(17); // EEXIST
    case vm::Error::ResourceLimit:
    case vm::Error::OutOfMemory:
    case vm::Error::MappingFailed: return Error(12); // ENOMEM
    }
    return Error(22);
}
}

std::uint64_t SyntheticSession::HandleGuestAllocate() {
    const auto permissions = cpu_.context.x[1];
    if (permissions > UINT8_MAX) return Error(22);
    const auto result = arena_.Allocate(cpu_.context.x[0],
                        static_cast<Access>(permissions), cpu_.context.x[2]);
    return result.Ok() ? result.region.base : VirtualError(result.error);
}

std::uint64_t SyntheticSession::HandleGuestFree() {
    return VirtualError(arena_.Free(cpu_.context.x[0]));
}

std::uint64_t SyntheticSession::HandleGuestProtect() {
    if (cpu_.context.x[1] > UINT8_MAX) return Error(22);
    return VirtualError(arena_.Protect(cpu_.context.x[0],
                        static_cast<Access>(cpu_.context.x[1])));
}

std::uint64_t SyntheticSession::HandleGuestQuery() {
    const auto result = arena_.Query(cpu_.context.x[0]);
    if (!result.Ok()) return VirtualError(result.error);
    std::array<std::uint8_t, 24> record{};
    Store64(record, 0, result.region.base);
    Store64(record, 8, result.region.size);
    Store64(record, 16, static_cast<std::uint8_t>(result.region.access));
    return memory_.WriteBuffer(cpu_.context.x[1], record) ? 0 : Error(14);
}

SessionResult SyntheticSession::Run(std::uint64_t max_steps) {
    if (exited_) return Outcome(SessionStop::Exited, CpuStop::SupervisorCall);
    assets_.Seal(); // immutable manifest once a guest may observe it
    if (waiting_ && !CanResume())
        return Outcome(SessionStop::Blocked, CpuStop::SupervisorCall);
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
            RetireHandles(); // departed guests cannot keep peers/files artificially alive
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
        } else if (service == 10) { // Open read-only asset by guest UTF-8 path bytes
            cpu_.context.x[0] = HandleAssetOpen();
        } else if (service == 11) { // Read into guest memory, advancing fd on success
            cpu_.context.x[0] = HandleAssetRead(false);
        } else if (service == 12) { // Signed relative/absolute seek
            cpu_.context.x[0] = HandleAssetSeek();
        } else if (service == 13) { // Close asset descriptor
            cpu_.context.x[0] = HandleAssetClose();
        } else if (service == 14) { // Read path metadata into guest memory
            cpu_.context.x[0] = HandleAssetStat();
        } else if (service == 15) { // Enumerate an immutable path record
            cpu_.context.x[0] = HandleAssetEnumerate();
        } else if (service == 16) { // Read without changing the current offset
            cpu_.context.x[0] = HandleAssetRead(true);
        } else if (service == 17) { // Descriptor metadata including offset
            cpu_.context.x[0] = HandleAssetFstat();
        } else if (service == 18) { // Map bounded private guest virtual pages
            cpu_.context.x[0] = HandleGuestAllocate();
        } else if (service == 19) { // Release an exact original arena region
            cpu_.context.x[0] = HandleGuestFree();
        } else if (service == 20) { // Change W^X checked page permissions
            cpu_.context.x[0] = HandleGuestProtect();
        } else if (service == 21) { // Query ownership and page attributes
            cpu_.context.x[0] = HandleGuestQuery();
        } else if (service == 9) { // Query or cooperatively wait for queue readiness
            std::uint32_t handle{};
            const auto raw_direction = cpu_.context.x[1];
            const auto mode = cpu_.context.x[2];
            if (!DecodeHandle(cpu_.context.x[0], handle)) {
                cpu_.context.x[0] = Error(9);
            } else if ((raw_direction != 1 && raw_direction != 2) || mode > 1) {
                cpu_.context.x[0] = Error(22);
            } else {
                const auto direction = static_cast<kernel::Right>(raw_direction);
                const auto status = handles_.Status(handle, direction);
                if (mode == 1 && (status.error == kernel::Error::Empty ||
                                  status.error == kernel::Error::Full)) {
                    waiting_ = true;
                    wait_handle_ = handle;
                    wait_direction_ = direction;
                    return Outcome(SessionStop::Blocked, step.stop);
                }
                cpu_.context.x[0] = QueueError(status.error);
            }
        } else {
            return Outcome(SessionStop::UnsupportedSupervisorCall, step.stop,
                           cpu_.context.pc, service);
        }
        waiting_ = false;
        cpu_.context.pc += 4; // SVC is a four-byte AArch64 instruction
    }
    return Outcome(SessionStop::StepLimit, CpuStop::StepLimit, cpu_.context.pc);
}
} // namespace nint2
