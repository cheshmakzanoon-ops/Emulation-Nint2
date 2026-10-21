#include "nint2/arena.hpp"
#include <iterator>
#include <new>

namespace nint2::vm {
namespace {
constexpr std::uint64_t kPage = GuestMemory::kPageSize;
}

GuestArena::~GuestArena() noexcept {
    // The caller keeps GuestMemory alive throughout this arena's lifetime.
    for (const auto& [base, region] : regions_) {
        (void)base;
        (void)memory_.Unmap(region.base, region.size);
    }
}

bool GuestArena::ValidAccess(Access flags) noexcept {
    const auto bits = static_cast<std::uint8_t>(flags);
    return (bits & ~std::uint8_t{7}) == 0 &&
           !(HasAccess(flags, Access::Write) && HasAccess(flags, Access::Execute));
}

bool GuestArena::ValidWindow() const noexcept {
    return length_ > 0 && base_ % kPage == 0 && length_ % kPage == 0 &&
           base_ < UINT64_MAX - length_ &&
           base_ < memory_.AddressLimit() &&
           length_ <= memory_.AddressLimit() - base_;
}

bool GuestArena::InWindow(std::uint64_t address, std::uint64_t size) const noexcept {
    return ValidWindow() && size != 0 && address >= base_ &&
           address < base_ + length_ && size <= base_ + length_ - address;
}

bool GuestArena::Overlaps(std::uint64_t address, std::uint64_t size) const noexcept {
    const auto it = regions_.lower_bound(address);
    if (it != regions_.end() && it->first < address + size) return true;
    if (it != regions_.begin()) {
        const auto previous = std::prev(it);
        if (previous->first + previous->second.size > address) return true;
    }
    return false;
}

Result GuestArena::Install(std::uint64_t address, std::uint64_t size, Access access) {
    if (!memory_.Map(address, size, access))
        return {Error::MappingFailed, {}};
    try {
        const Region region{address, size, access};
        const auto [it, inserted] = regions_.emplace(address, region);
        if (!inserted) {
            (void)memory_.Unmap(address, size);
            return {Error::AddressInUse, {}};
        }
        allocated_ += size;
        return {Error::None, it->second};
    } catch (const std::bad_alloc&) {
        (void)memory_.Unmap(address, size);
        return {Error::OutOfMemory, {}};
    }
}

Result GuestArena::Allocate(std::uint64_t size, Access access, std::uint64_t preferred) {
    if (size == 0 || size % kPage != 0 || size > kMaxAllocation || size > length_ ||
        !ValidAccess(access) || !ValidWindow())
        return {Error::InvalidArgument, {}};
    if (regions_.size() >= kMaxRegions || size > kMaxMappedBytes - allocated_)
        return {Error::ResourceLimit, {}};

    if (preferred != 0) {
        if (preferred % kPage != 0 || !InWindow(preferred, size))
            return {Error::InvalidArgument, {}};
        if (Overlaps(preferred, size)) return {Error::AddressInUse, {}};
        return Install(preferred, size, access);
    }

    // Scan ascending page addresses. Skipping known regions avoids retrying
    // the same occupied range; GuestMemory additionally checks external maps.
    for (std::uint64_t candidate = base_; candidate <= base_ + length_ - size;) {
        const auto it = regions_.lower_bound(candidate);
        if (it != regions_.begin()) {
            const auto previous = std::prev(it);
            if (previous->first + previous->second.size > candidate) {
                candidate = previous->first + previous->second.size;
                continue;
            }
        }
        if (it != regions_.end() && it->first < candidate + size) {
            candidate = it->first + it->second.size;
            continue;
        }
        const auto placed = Install(candidate, size, access);
        if (placed.Ok() || placed.error == Error::OutOfMemory) return placed;
        // An unrelated host/bootstrap segment may occupy candidate. Move a
        // page forward; next iteration preserves deterministic first fit.
        candidate += kPage;
    }
    return {Error::ResourceLimit, {}};
}

Error GuestArena::Free(std::uint64_t base) noexcept {
    const auto it = regions_.find(base);
    if (it == regions_.end()) return Error::InvalidRegion;
    if (!memory_.Unmap(it->second.base, it->second.size)) return Error::MappingFailed;
    allocated_ -= it->second.size;
    regions_.erase(it);
    return Error::None;
}

Error GuestArena::Protect(std::uint64_t base, Access access) noexcept {
    if (!ValidAccess(access)) return Error::InvalidArgument;
    const auto it = regions_.find(base);
    if (it == regions_.end()) return Error::InvalidRegion;
    if (!memory_.Protect(base, it->second.size, access))
        return Error::MappingFailed;
    it->second.access = access;
    return Error::None;
}

Result GuestArena::Query(std::uint64_t address) const noexcept {
    auto next = regions_.upper_bound(address);
    if (next == regions_.begin()) return {Error::InvalidRegion, {}};
    --next;
    const auto& region = next->second;
    if (address - region.base >= region.size) return {Error::InvalidRegion, {}};
    return {Error::None, region};
}
} // namespace nint2::vm
