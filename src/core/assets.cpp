#include "nint2/assets.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace nint2::assets {
namespace {
[[nodiscard]] bool Allowed(char character) noexcept {
    const unsigned char c = static_cast<unsigned char>(character);
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}
} // namespace

bool AssetStore::ValidPath(std::string_view path) noexcept {
    if (path.size() < 2 || path.size() > kMaxPath || path.front() != '/') return false;
    std::size_t start = 1;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto length = (end == std::string_view::npos ? path.size() : end) - start;
        if (length == 0 || (length == 1 && path[start] == '.') ||
            (length == 2 && path.substr(start, length) == "..")) return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!Allowed(path[start + i])) return false;
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
    return false; // trailing slash
}

std::uint64_t AssetStore::Hash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

Result AssetStore::Mount(std::string_view path, std::span<const std::uint8_t> bytes) {
    if (sealed_) return {Error::Sealed, 0};
    if (!ValidPath(path)) return {Error::InvalidPath, 0};
    if (files_.contains(path)) return {Error::Duplicate, 0};
    if (files_.size() >= kMaxAssets) return {Error::TooManyAssets, 0};
    if (bytes.size() > kMaxFileBytes) return {Error::TooLarge, 0};
    if (bytes.size() > kMaxTotalBytes - stored_bytes_) return {Error::NoSpace, 0};

    // Build a complete immutable value before publication; allocation failure
    // leaves the manifest and quotas untouched.
    try {
        File file{std::vector<std::uint8_t>(bytes.begin(), bytes.end()), Hash(bytes)};
        const auto [it, inserted] = files_.emplace(std::string(path), std::move(file));
        (void)it;
        if (!inserted) return {Error::Duplicate, 0};
    } catch (const std::bad_alloc&) {
        return {Error::OutOfMemory, 0};
    } catch (const std::length_error&) {
        return {Error::OutOfMemory, 0};
    }
    stored_bytes_ += bytes.size();
    return {Error::None, bytes.size()};
}

Result AssetStore::Open(std::string_view path) {
    if (!ValidPath(path)) return {Error::InvalidPath, 0};
    const auto file = files_.find(path);
    if (file == files_.end()) return {Error::NotFound, 0};
    if (handles_.size() >= kMaxHandles || next_handle_ == 0)
        return {Error::TooManyHandles, 0};
    const auto handle = next_handle_;
    try {
        const auto [it, inserted] = handles_.emplace(handle, Descriptor{file, 0});
        (void)it;
        if (!inserted) return {Error::TooManyHandles, 0};
    } catch (const std::bad_alloc&) {
        return {Error::OutOfMemory, 0};
    }
    ++next_handle_;
    return {Error::None, handle};
}

Result AssetStore::Close(std::uint32_t handle) noexcept {
    if (handle < kFirstHandle || handles_.erase(handle) != 1)
        return {Error::InvalidHandle, 0};
    return {Error::None, 0};
}

Result AssetStore::Copy(const File& file, std::uint64_t offset,
                        std::span<std::uint8_t> output) noexcept {
    if (output.size() > kMaxRead) return {Error::InvalidArgument, 0};
    if (offset >= file.bytes.size()) return {Error::None, 0};
    const auto remaining = file.bytes.size() - static_cast<std::size_t>(offset);
    const auto count = std::min(output.size(), remaining);
    std::copy_n(file.bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                static_cast<std::ptrdiff_t>(count), output.begin());
    return {Error::None, count};
}

Result AssetStore::Peek(std::uint32_t handle,
                        std::span<std::uint8_t> output) const noexcept {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return {Error::InvalidHandle, 0};
    return Copy(it->second.file->second, it->second.offset, output);
}

Result AssetStore::PeekAt(std::uint32_t handle, std::uint64_t offset,
                          std::span<std::uint8_t> output) const noexcept {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return {Error::InvalidHandle, 0};
    if (offset > kMaxSeek) return {Error::InvalidArgument, 0};
    return Copy(it->second.file->second, offset, output);
}

Result AssetStore::Consume(std::uint32_t handle, std::size_t count) noexcept {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return {Error::InvalidHandle, 0};
    const auto length = it->second.file->second.bytes.size();
    const auto offset = it->second.offset;
    const auto remaining = offset >= length ? 0 : length - static_cast<std::size_t>(offset);
    if (count > remaining || count > kMaxSeek - offset)
        return {Error::InvalidArgument, 0};
    it->second.offset += count;
    return {Error::None, it->second.offset};
}

Result AssetStore::Seek(std::uint32_t handle, std::int64_t delta,
                        unsigned origin) noexcept {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return {Error::InvalidHandle, 0};
    std::uint64_t base = 0;
    if (origin == 1) base = it->second.offset;
    else if (origin == 2) base = it->second.file->second.bytes.size();
    else if (origin != 0) return {Error::InvalidArgument, 0};

    std::uint64_t next = 0;
    if (delta >= 0) {
        const auto amount = static_cast<std::uint64_t>(delta);
        if (base > kMaxSeek || amount > kMaxSeek - base)
            return {Error::InvalidArgument, 0};
        next = base + amount;
    } else {
        // Works even for INT64_MIN, whose negation is not representable.
        const auto amount = static_cast<std::uint64_t>(-(delta + 1)) + 1;
        if (amount > base) return {Error::InvalidArgument, 0};
        next = base - amount;
    }
    it->second.offset = next;
    return {Error::None, next};
}

Error AssetStore::Stat(std::string_view path, Info& info) const noexcept {
    if (!ValidPath(path)) return Error::InvalidPath;
    const auto it = files_.find(path);
    if (it == files_.end()) return Error::NotFound;
    info = {it->second.bytes.size(), it->second.digest, 0};
    return Error::None;
}

Error AssetStore::Fstat(std::uint32_t handle, Info& info) const noexcept {
    const auto it = handles_.find(handle);
    if (it == handles_.end()) return Error::InvalidHandle;
    info = {it->second.file->second.bytes.size(), it->second.file->second.digest,
            it->second.offset};
    return Error::None;
}

bool AssetStore::Enumerate(std::size_t index, std::string_view& name,
                           Info& info) const noexcept {
    if (index >= files_.size()) return false;
    auto it = files_.cbegin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    name = it->first;
    info = {it->second.bytes.size(), it->second.digest, 0};
    return true;
}
} // namespace nint2::assets
