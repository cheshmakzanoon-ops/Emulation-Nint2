#include "nint2/memory.hpp"
#include <new>

namespace nint2 {
GuestMemory::GuestMemory(std::uint64_t address_limit) noexcept
    : address_limit_(address_limit) {}

bool GuestMemory::ValidPageRange(std::uint64_t base, std::uint64_t size) const noexcept {
    return size != 0 && base % kPageSize == 0 && size % kPageSize == 0 &&
           base < address_limit_ && size <= address_limit_ - base;
}

bool GuestMemory::SecureAccess(Access access) noexcept {
    const auto bits = static_cast<std::uint8_t>(access);
    return (bits & ~std::uint8_t{7}) == 0 &&
           !(HasAccess(access, Access::Write) && HasAccess(access, Access::Execute));
}

bool GuestMemory::FrameAccessAllowed(const std::shared_ptr<Frame>& frame, Access access,
                                     std::uint64_t ignore_base, std::uint64_t ignore_size) const {
    for (const auto& [index, page] : pages_) {
        const std::uint64_t address = index * kPageSize;
        if (ignore_size != 0 && address >= ignore_base && address - ignore_base < ignore_size)
            continue;
        if (page.frame != frame) continue;
        if ((HasAccess(access, Access::Write) && HasAccess(page.access, Access::Execute)) ||
            (HasAccess(access, Access::Execute) && HasAccess(page.access, Access::Write)))
            return false;
    }
    return true;
}

bool GuestMemory::Map(std::uint64_t base, std::uint64_t size, Access access) {
    if (!ValidPageRange(base, size) || !SecureAccess(access)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize)
        if (pages_.contains((base + offset) / kPageSize)) return false;
    std::uint64_t inserted_bytes = 0;
    try {
        for (; inserted_bytes < size; inserted_bytes += kPageSize) {
            const auto index = (base + inserted_bytes) / kPageSize;
            pages_.emplace(index, Page{std::make_shared<Frame>(), access});
        }
    } catch (const std::bad_alloc&) {
        for (std::uint64_t offset = 0; offset < inserted_bytes; offset += kPageSize)
            pages_.erase((base + offset) / kPageSize);
        return false;
    }
    return true;
}

bool GuestMemory::Alias(std::uint64_t dst, std::uint64_t src, std::uint64_t size, Access access) {
    if (!ValidPageRange(dst, size) || !ValidPageRange(src, size) || !SecureAccess(access))
        return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        const auto source = pages_.find((src + offset) / kPageSize);
        if (source == pages_.end() || pages_.contains((dst + offset) / kPageSize) ||
            !FrameAccessAllowed(source->second.frame, access)) return false;
    }
    std::uint64_t inserted_bytes = 0;
    try {
        for (; inserted_bytes < size; inserted_bytes += kPageSize) {
            const auto& source = pages_.at((src + inserted_bytes) / kPageSize);
            pages_.emplace((dst + inserted_bytes) / kPageSize, Page{source.frame, access});
        }
    } catch (const std::bad_alloc&) {
        for (std::uint64_t offset = 0; offset < inserted_bytes; offset += kPageSize)
            pages_.erase((dst + offset) / kPageSize);
        return false;
    }
    return true;
}

bool GuestMemory::Unmap(std::uint64_t base, std::uint64_t size) {
    if (!ValidPageRange(base, size)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize)
        if (!pages_.contains((base + offset) / kPageSize)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize)
        pages_.erase((base + offset) / kPageSize);
    return true;
}

bool GuestMemory::Protect(std::uint64_t base, std::uint64_t size, Access access) {
    if (!ValidPageRange(base, size) || !SecureAccess(access)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        const auto it = pages_.find((base + offset) / kPageSize);
        if (it == pages_.end() ||
            !FrameAccessAllowed(it->second.frame, access, base, size)) return false;
    }
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize)
        pages_.at((base + offset) / kPageSize).access = access;
    return true;
}

std::optional<std::uint64_t> GuestMemory::ReadLe(std::uint64_t address, unsigned bytes,
                                                 Access access) const {
    if (address >= address_limit_ || bytes > address_limit_ - address) return std::nullopt;
    std::uint64_t result = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        const auto addr = address + i;
        const auto it = pages_.find(addr / kPageSize);
        if (it == pages_.end() || !HasAccess(it->second.access, access)) return std::nullopt;
        const std::uint8_t value = it->second.frame->data ? (*it->second.frame->data)[addr % kPageSize] : std::uint8_t{0};
        result |= std::uint64_t{value} << (8 * i);
    }
    return result;
}

bool GuestMemory::WriteLe(std::uint64_t address, std::uint64_t value, unsigned bytes) {
    if (address >= address_limit_ || bytes > address_limit_ - address) return false;
    for (unsigned i = 0; i < bytes; ++i) {
        const auto addr = address + i;
        const auto it = pages_.find(addr / kPageSize);
        if (it == pages_.end() || !HasAccess(it->second.access, Access::Write)) return false;
    }
    try {
        for (unsigned i = 0; i < bytes; ++i) {
            const auto addr = address + i;
            auto& data = pages_.at(addr / kPageSize).frame->data;
            if (!data) data = std::make_unique<std::array<std::uint8_t, kPageSize>>();
        }
    } catch (const std::bad_alloc&) { return false; }
    for (unsigned i = 0; i < bytes; ++i) {
        const auto addr = address + i;
        (*pages_.at(addr / kPageSize).frame->data)[addr % kPageSize] =
            static_cast<std::uint8_t>(value >> (8 * i));
    }
    return true;
}

std::optional<std::uint8_t> GuestMemory::Read8(std::uint64_t a) const {
    if (const auto v = ReadLe(a, 1, Access::Read)) return static_cast<std::uint8_t>(*v);
    return std::nullopt;
}
std::optional<std::uint16_t> GuestMemory::Read16(std::uint64_t a) const {
    if (const auto v = ReadLe(a, 2, Access::Read)) return static_cast<std::uint16_t>(*v);
    return std::nullopt;
}
std::optional<std::uint32_t> GuestMemory::Read32(std::uint64_t a) const {
    if (const auto v = ReadLe(a, 4, Access::Read)) return static_cast<std::uint32_t>(*v);
    return std::nullopt;
}
std::optional<std::uint64_t> GuestMemory::Read64(std::uint64_t a) const {
    return ReadLe(a, 8, Access::Read);
}
std::optional<std::uint32_t> GuestMemory::Fetch32(std::uint64_t pc) const {
    if (pc % 4 != 0) return std::nullopt;
    if (const auto v = ReadLe(pc, 4, Access::Execute)) return static_cast<std::uint32_t>(*v);
    return std::nullopt;
}
bool GuestMemory::Write8(std::uint64_t a, std::uint8_t v) { return WriteLe(a, v, 1); }
bool GuestMemory::Write16(std::uint64_t a, std::uint16_t v) { return WriteLe(a, v, 2); }
bool GuestMemory::Write32(std::uint64_t a, std::uint32_t v) { return WriteLe(a, v, 4); }
bool GuestMemory::Write64(std::uint64_t a, std::uint64_t v) { return WriteLe(a, v, 8); }
} // namespace nint2
