#include "nint2/kernel.hpp"
#include <algorithm>
#include <new>

namespace nint2::kernel {
namespace {
constexpr bool ValidRights(Right rights) noexcept {
    const auto bits = static_cast<std::uint8_t>(rights);
    return bits != 0 && (bits & ~std::uint8_t{7}) == 0;
}
}
Result MessageQueue::Send(std::span<const std::uint8_t> message) {
    if (message.size() > kMaxMessage) return {Error::InvalidArgument, 0};
    if (capacity_ == 0 || capacity_ > kMaxMessages) return {Error::InvalidArgument, 0};
    if (messages_.size() >= capacity_ || message.size() > kMaxQueuedBytes - queued_bytes_)
        return {Error::Full, 0};
    try {
        messages_.emplace_back(message.begin(), message.end());
    } catch (const std::bad_alloc&) {
        return {Error::OutOfMemory, 0};
    }
    queued_bytes_ += message.size();
    return {Error::None, message.size()};
}
Result MessageQueue::Peek(std::span<std::uint8_t> destination) const {
    if (messages_.empty()) return {Error::Empty, 0};
    const auto& message = messages_.front();
    if (destination.size() < message.size()) return {Error::BufferTooSmall, message.size()};
    std::copy(message.begin(), message.end(), destination.begin());
    return {Error::None, message.size()};
}
Result MessageQueue::Consume() noexcept {
    if (messages_.empty()) return {Error::Empty, 0};
    const std::size_t size = messages_.front().size();
    queued_bytes_ -= size;
    messages_.pop_front();
    return {Error::None, size};
}
Result HandleTable::Insert(std::shared_ptr<MessageQueue> queue, Right rights) {
    if (!queue || !ValidRights(rights)) return {Error::InvalidArgument, 0};
    if (entries_.size() >= capacity_ || next_handle_ == 0)
        return {Error::Exhausted, 0};
    const auto handle = next_handle_;
    try {
        const auto [iter, inserted] = entries_.emplace(handle, Entry{std::move(queue), rights});
        (void)iter;
        if (!inserted) return {Error::Exhausted, 0};
    } catch (const std::bad_alloc&) {
        return {Error::OutOfMemory, 0};
    }
    ++next_handle_; // wraps to zero after UINT32_MAX; permanently exhausted
    return {Error::None, handle};
}
Result HandleTable::CreateQueue(std::size_t queue_capacity) {
    if (queue_capacity == 0 || queue_capacity > MessageQueue::kMaxMessages)
        return {Error::InvalidArgument, 0};
    try {
        return Insert(std::make_shared<MessageQueue>(queue_capacity),
                      Right::Send | Right::Receive | Right::Duplicate);
    } catch (const std::bad_alloc&) {
        return {Error::OutOfMemory, 0};
    }
}
Error HandleTable::Check(std::uint32_t handle, Right required) const noexcept {
    const auto it = entries_.find(handle);
    if (handle == 0 || it == entries_.end()) return Error::InvalidHandle;
    return HasRights(it->second.rights, required) ? Error::None : Error::PermissionDenied;
}
std::shared_ptr<MessageQueue> HandleTable::Get(std::uint32_t handle,
                                               Right required) const noexcept {
    if (Check(handle, required) != Error::None) return {};
    return entries_.at(handle).queue;
}
Result HandleTable::Duplicate(std::uint32_t handle, Right requested) {
    const auto it = entries_.find(handle);
    if (handle == 0 || it == entries_.end()) return {Error::InvalidHandle, 0};
    if (!HasRights(it->second.rights, Right::Duplicate)) return {Error::PermissionDenied, 0};
    if (!ValidRights(requested) || !HasRights(it->second.rights, requested))
        return {Error::PermissionDenied, 0};
    return Insert(it->second.queue, requested);
}
Result HandleTable::GrantTo(std::uint32_t handle, HandleTable& recipient,
                            Right requested) const {
    const auto it = entries_.find(handle);
    if (handle == 0 || it == entries_.end()) return {Error::InvalidHandle, 0};
    if (!HasRights(it->second.rights, Right::Duplicate))
        return {Error::PermissionDenied, 0};
    if (!ValidRights(requested) || !HasRights(it->second.rights, requested))
        return {Error::PermissionDenied, 0};
    return recipient.Insert(it->second.queue, requested);
}
Result HandleTable::Close(std::uint32_t handle) noexcept {
    if (handle == 0 || entries_.erase(handle) != 1) return {Error::InvalidHandle, 0};
    return {Error::None, 0};
}
} // namespace nint2::kernel
