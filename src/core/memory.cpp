#include "nint2/memory.hpp"

namespace nint2 {
GuestMemory::GuestMemory(std::uint64_t address_limit) noexcept
    : address_limit_(address_limit) {}

bool GuestMemory::ValidPageRange(std::uint64_t base, std::uint64_t size) const noexcept {
    return size != 0 && base % kPageSize == 0 && size % kPageSize == 0 &&
           base < address_limit_ && size <= address_limit_ - base;
}

bool GuestMemory::SecureAccess(Access access) noexcept {
    return !HasAccess(access, Access::Write) || !HasAccess(access, Access::Execute);
}

bool GuestMemory::Map(std::uint64_t base, std::uint64_t size, Access access) {
    if (!ValidPageRange(base, size) || !SecureAccess(access)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        if (pages_.contains((base + offset) / kPageSize)) return false;
    }
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        auto [it, inserted] = pages_.try_emplace((base + offset) / kPageSize);
        (void)inserted;
        it->second.access = access;
    }
    return true;
}

bool GuestMemory::Unmap(std::uint64_t base, std::uint64_t size) {
    if (!ValidPageRange(base, size)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        if (!pages_.contains((base + offset) / kPageSize)) return false;
    }
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        pages_.erase((base + offset) / kPageSize);
    }
    return true;
}

bool GuestMemory::Protect(std::uint64_t base, std::uint64_t size, Access access) {
    if (!ValidPageRange(base, size) || !SecureAccess(access)) return false;
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        if (!pages_.contains((base + offset) / kPageSize)) return false;
    }
    for (std::uint64_t offset = 0; offset < size; offset += kPageSize) {
        pages_.at((base + offset) / kPageSize).access = access;
    }
    return true;
}

std::optional<std::uint8_t> GuestMemory::Read8(std::uint64_t address) const {
    if (address >= address_limit_) return std::nullopt;
    const auto it = pages_.find(address / kPageSize);
    if (it == pages_.end() || !HasAccess(it->second.access, Access::Read)) return std::nullopt;
    return it->second.data[address % kPageSize];
}
bool GuestMemory::Write8(std::uint64_t address, std::uint8_t value) {
    if (address >= address_limit_) return false;
    const auto it = pages_.find(address / kPageSize);
    if (it == pages_.end() || !HasAccess(it->second.access, Access::Write)) return false;
    it->second.data[address % kPageSize] = value;
    return true;
}
} // namespace nint2
