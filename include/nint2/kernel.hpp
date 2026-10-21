#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace nint2 {
namespace kernel {

enum class Right : std::uint8_t { None = 0, Send = 1, Receive = 2, Duplicate = 4 };
constexpr Right operator|(Right a, Right b) noexcept {
    return static_cast<Right>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr bool HasRights(Right owned, Right required) noexcept {
    return (static_cast<std::uint8_t>(owned) & static_cast<std::uint8_t>(required)) ==
           static_cast<std::uint8_t>(required);
}

enum class Error : std::uint8_t {
    None, InvalidHandle, PermissionDenied, InvalidArgument, Exhausted,
    Empty, Full, BufferTooSmall, OutOfMemory
};
struct Result {
    Error error{Error::None};
    std::uint64_t value{};
    [[nodiscard]] constexpr bool Ok() const noexcept { return error == Error::None; }
};

// Single-threaded, bounded, FIFO message queue. The caller validates all guest
// pointers; the queue never accesses host or guest process address spaces.
class MessageQueue final {
public:
    static constexpr std::size_t kMaxMessage = 4096;
    static constexpr std::size_t kMaxQueuedBytes = 65536;
    static constexpr std::size_t kMaxMessages = 64;
    explicit MessageQueue(std::size_t capacity) noexcept : capacity_(capacity) {}
    [[nodiscard]] Result Send(std::span<const std::uint8_t> message);
    [[nodiscard]] Result Peek(std::span<std::uint8_t> destination) const;
    [[nodiscard]] Result Consume() noexcept;
    [[nodiscard]] std::size_t Depth() const noexcept { return messages_.size(); }
    [[nodiscard]] std::size_t QueuedBytes() const noexcept { return queued_bytes_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
private:
    std::size_t capacity_{};
    std::size_t queued_bytes_{};
    std::deque<std::vector<std::uint8_t>> messages_;
};

// Monotonically allocated 32-bit handles avoid stale-handle/ABA reuse within a
// process. Closing a handle never invalidates other aliases to the same queue.
// Not thread-safe: deterministic single-core kernel fixture, not Horizon ABI.
class HandleTable final {
public:
    static constexpr std::size_t kDefaultCapacity = 256;
    explicit HandleTable(std::size_t capacity = kDefaultCapacity) noexcept
        : capacity_(capacity) {}
    [[nodiscard]] Result CreateQueue(std::size_t queue_capacity);
    [[nodiscard]] Result Duplicate(std::uint32_t handle, Right requested);
    // Host-controlled capability grant into a distinct guest handle namespace.
    // Requires Duplicate authority; never removes the sender's handle/rights.
    [[nodiscard]] Result GrantTo(std::uint32_t handle, HandleTable& recipient,
                                 Right requested) const;
    [[nodiscard]] Result Close(std::uint32_t handle) noexcept;
    [[nodiscard]] std::shared_ptr<MessageQueue> Get(std::uint32_t handle,
                                                    Right required) const noexcept;
    [[nodiscard]] Error Check(std::uint32_t handle, Right required) const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
private:
    struct Entry {
        std::shared_ptr<MessageQueue> queue;
        Right rights{Right::None};
    };
    [[nodiscard]] Result Insert(std::shared_ptr<MessageQueue> queue, Right rights);
    std::size_t capacity_{};
    std::uint32_t next_handle_{1};
    std::unordered_map<std::uint32_t, Entry> entries_;
};
} // namespace kernel
} // namespace nint2
