#include "nint2/memory.hpp"
#include <new>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

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
            if (next_frame_id_ == 0) throw std::bad_alloc{};
            auto frame = std::make_shared<Frame>();
            frame->id = next_frame_id_++;
            pages_.emplace(index, Page{std::move(frame), access});
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
        auto& frame = *pages_.at(addr / kPageSize).frame;
        (*frame.data)[addr % kPageSize] =
            static_cast<std::uint8_t>(value >> (8 * i));
        ++frame.generation;
    }
    return true;
}

bool GuestMemory::WriteBuffer(std::uint64_t address,
                              std::span<const std::uint8_t> data) {
    if (data.empty()) return true;
    if (address >= address_limit_ || data.size() > address_limit_ - address)
        return false;
    struct Chunk {
        Frame* frame;
        std::size_t page_offset, source_offset, length;
    };
    std::vector<Chunk> chunks;
    std::vector<std::pair<Frame*, std::unique_ptr<std::array<std::uint8_t,kPageSize>>>> fresh;
    std::unordered_set<Frame*> needs_backing;
    // Validate every destination page BEFORE mutating backing frames or bytes.
    // The input may span writable and unmapped/protected pages in one request.
    try {
        chunks.reserve(1 + (data.size() - 1) / kPageSize);
        std::size_t i = 0;
        while (i < data.size()) {
            const auto current = address + i;
            const auto page = pages_.find(current / kPageSize);
            if (page == pages_.end() ||
                !HasAccess(page->second.access, Access::Write)) return false;
            const auto offset = static_cast<std::size_t>(current % kPageSize);
            const auto count = std::min<std::size_t>(data.size() - i, kPageSize - offset);
            Frame* const frame = page->second.frame.get();
            chunks.push_back({frame, offset, i, count});
            if (!frame->data && needs_backing.insert(frame).second)
                fresh.emplace_back(frame,
                    std::make_unique<std::array<std::uint8_t,kPageSize>>());
            i += count;
        }
    } catch (const std::bad_alloc&) {
        return false;
    }
    // All allocations are complete, so the remaining commit cannot partially
    // fault. Aliases share the same physical frame and its new backing.
    for (auto& [frame, backing] : fresh) frame->data = std::move(backing);
    for (const auto& chunk : chunks) {
        std::memcpy(chunk.frame->data->data() + chunk.page_offset,
                    data.data() + chunk.source_offset, chunk.length);
        // A single generation bump per page chunk is enough to invalidate
        // all exclusive reservations in this single-thread host interpreter.
        ++chunk.frame->generation;
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

std::optional<GuestMemory::ExclusiveRead> GuestMemory::LoadExclusive(
    std::uint64_t address, unsigned bytes) const {
    if ((bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ||
        address % bytes != 0) return std::nullopt;
    const auto value = ReadLe(address, bytes, Access::Read);
    if (!value) return std::nullopt;
    // Natural alignment and a page size divisible by 8 ensure all exclusive
    // bytes belong to exactly one shared backing frame, including aliases.
    const auto page = pages_.find(address / kPageSize);
    if (page == pages_.end()) return std::nullopt;
    return ExclusiveRead{*value, page->second.frame->id,
                         page->second.frame->generation};
}
GuestMemory::ExclusiveStore GuestMemory::StoreExclusive(
    std::uint64_t address, unsigned bytes, std::uint64_t value,
    std::uint64_t frame_id, std::uint64_t generation) {
    if ((bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ||
        address % bytes != 0 || address >= address_limit_ ||
        bytes > address_limit_ - address)
        return ExclusiveStore::Fault;
    auto page = pages_.find(address / kPageSize);
    if (page == pages_.end() ||
        !HasAccess(page->second.access, Access::Write))
        return ExclusiveStore::Fault;
    const auto& frame = *page->second.frame;
    // No matching reservation -> normal STXR failure without memory writes.
    if (frame_id == 0 || frame.id != frame_id ||
        frame.generation != generation)
        return ExclusiveStore::ReservationLost;
    // All condition checks and the write run in one host thread; no native
    // locks or pointer aliases are exposed to guest code.
    if (!WriteLe(address, value, bytes))
        return ExclusiveStore::Fault;
    return ExclusiveStore::Stored;
}

std::optional<std::uint64_t> GuestMemory::AtomicRmw(
    std::uint64_t address, unsigned bytes, AtomicOperation operation,
    std::uint64_t operand, std::uint64_t expected) {
    // All LSE operand sizes are naturally aligned. A naturally aligned
    // operand of at most eight bytes cannot span a 4 KiB page boundary.
    if ((bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ||
        address % bytes != 0 || address >= address_limit_ ||
        bytes > address_limit_ - address)
        return std::nullopt;

    const auto page = pages_.find(address / kPageSize);
    if (page == pages_.end() ||
        !HasAccess(page->second.access, Access::Read | Access::Write))
        return std::nullopt;
    const auto previous = ReadLe(address, bytes, Access::Read);
    if (!previous) return std::nullopt;

    const auto mask = bytes == 8 ? UINT64_MAX :
                      (std::uint64_t{1} << (bytes * 8)) - 1;
    const auto old = *previous & mask;
    const auto rhs = operand & mask;
    const auto sign = std::uint64_t{1} << (bytes * 8 - 1);
    std::uint64_t next = old;
    bool write = true;
    switch (operation) {
    case AtomicOperation::Swap: next = rhs; break;
    case AtomicOperation::Add: next = (old + rhs) & mask; break;
    case AtomicOperation::Clear: next = old & ~rhs; break;
    case AtomicOperation::Xor: next = old ^ rhs; break;
    case AtomicOperation::Set: next = old | rhs; break;
    case AtomicOperation::SignedMax:
        next = (old ^ sign) >= (rhs ^ sign) ? old : rhs; break;
    case AtomicOperation::SignedMin:
        next = (old ^ sign) <= (rhs ^ sign) ? old : rhs; break;
    case AtomicOperation::UnsignedMax: next = old >= rhs ? old : rhs; break;
    case AtomicOperation::UnsignedMin: next = old <= rhs ? old : rhs; break;
    case AtomicOperation::CompareSwap:
        if (old == (expected & mask)) next = rhs;
        else write = false;
        break;
    default: return std::nullopt;
    }
    // WriteLe validates/allocates before touching data. Even an equal-value
    // store must bump the physical frame's generation to invalidate LDXR.
    if (write && !WriteLe(address, next, bytes))
        return std::nullopt;
    return old;
}


std::optional<GuestMemory::ExclusivePairRead> GuestMemory::LoadExclusivePair(
    std::uint64_t address, unsigned element_bytes) const {
    if ((element_bytes != 4 && element_bytes != 8) ||
        address % (2 * element_bytes) != 0 ||
        address >= address_limit_ ||
        2 * element_bytes > address_limit_ - address)
        return std::nullopt;
    const auto first = ReadLe(address, element_bytes, Access::Read);
    const auto second = ReadLe(address + element_bytes, element_bytes, Access::Read);
    if (!first || !second) return std::nullopt;
    // Natural alignment guarantees both elements occupy the same physical
    // page. Frame identities, not virtual aliases, define monitor ownership.
    const auto page = pages_.find(address / kPageSize);
    if (page == pages_.end()) return std::nullopt;
    return ExclusivePairRead{*first, *second, page->second.frame->id,
                             page->second.frame->generation};
}

GuestMemory::ExclusiveStore GuestMemory::StoreExclusivePair(
    std::uint64_t address, unsigned element_bytes,
    std::uint64_t first, std::uint64_t second,
    std::uint64_t frame_id, std::uint64_t generation) {
    if ((element_bytes != 4 && element_bytes != 8) ||
        address % (2 * element_bytes) != 0 ||
        address >= address_limit_ ||
        2 * element_bytes > address_limit_ - address)
        return ExclusiveStore::Fault;
    const auto page = pages_.find(address / kPageSize);
    if (page == pages_.end() ||
        !HasAccess(page->second.access, Access::Write))
        return ExclusiveStore::Fault;
    const auto& frame = *page->second.frame;
    if (frame_id == 0 || frame.id != frame_id ||
        frame.generation != generation)
        return ExclusiveStore::ReservationLost;

    std::array<std::uint8_t, 16> bytes{};
    for (unsigned i = 0; i < element_bytes; ++i) {
        bytes[i] = static_cast<std::uint8_t>(first >> (8 * i));
        bytes[element_bytes + i] = static_cast<std::uint8_t>(second >> (8 * i));
    }
    if (!WriteBuffer(address, std::span<const std::uint8_t>{
            bytes.data(), 2 * element_bytes}))
        return ExclusiveStore::Fault;
    return ExclusiveStore::Stored;
}

std::optional<GuestMemory::AtomicPairRead> GuestMemory::AtomicComparePair(
    std::uint64_t address, unsigned element_bytes,
    std::uint64_t expected_first, std::uint64_t expected_second,
    std::uint64_t desired_first, std::uint64_t desired_second) {
    if ((element_bytes != 4 && element_bytes != 8) ||
        address % (2 * element_bytes) != 0 ||
        address >= address_limit_ ||
        2 * element_bytes > address_limit_ - address)
        return std::nullopt;
    const auto page = pages_.find(address / kPageSize);
    if (page == pages_.end() ||
        !HasAccess(page->second.access, Access::Read | Access::Write))
        return std::nullopt;
    const auto lo = ReadLe(address, element_bytes, Access::Read);
    const auto hi = ReadLe(address + element_bytes, element_bytes, Access::Read);
    if (!lo || !hi) return std::nullopt;
    const auto mask = element_bytes == 8 ? UINT64_MAX : std::uint64_t{UINT32_MAX};
    if (*lo == (expected_first & mask) && *hi == (expected_second & mask)) {
        // Build both new values before writing any guest byte. WriteBuffer
        // performs access and allocation checks up front, then atomically
        // commits the contiguous pair in this one-thread interpreter.
        std::array<std::uint8_t, 16> bytes{};
        for (unsigned i = 0; i < element_bytes; ++i) {
            bytes[i] = static_cast<std::uint8_t>(desired_first >> (8 * i));
            bytes[element_bytes + i] =
                static_cast<std::uint8_t>(desired_second >> (8 * i));
        }
        if (!WriteBuffer(address, std::span<const std::uint8_t>{
                bytes.data(), 2 * element_bytes}))
            return std::nullopt;
    }
    return AtomicPairRead{*lo, *hi};
}

} // namespace nint2
