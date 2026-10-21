#pragma once
#include "nint2/memory.hpp"
#include <cstddef>
#include <cstdint>
#include <map>

namespace nint2::vm {

// Synthetic guest-virtual allocation arena, not a Horizon OS memory service.
// The backing GuestMemory enforces page permissions, lazy-zero frames and W^X.
// The arena owns only its own mappings; no host pointer ever enters the guest.
enum class Error : std::uint8_t {
    None, InvalidArgument, AddressInUse, ResourceLimit, MappingFailed,
    InvalidRegion, OutOfMemory
};

struct Region {
    std::uint64_t base{};
    std::uint64_t size{};
    Access access{Access::None};
};

struct Result {
    Error error{Error::None};
    Region region{};
    [[nodiscard]] constexpr bool Ok() const noexcept { return error == Error::None; }
};

class GuestArena final {
public:
    static constexpr std::uint64_t kBase = 0x10000000ULL;
    static constexpr std::uint64_t kLength = 0x10000000ULL;
    static constexpr std::uint64_t kMaxAllocation = 16ULL * 1024 * 1024;
    static constexpr std::uint64_t kMaxMappedBytes = 64ULL * 1024 * 1024;
    static constexpr std::size_t kMaxRegions = 128;

    explicit GuestArena(GuestMemory& memory, std::uint64_t base = kBase,
                        std::uint64_t length = kLength) noexcept
        : memory_(memory), base_(base), length_(length) {}
    ~GuestArena() noexcept;
    GuestArena(const GuestArena&) = delete;
    GuestArena& operator=(const GuestArena&) = delete;
    GuestArena(GuestArena&&) = delete;
    GuestArena& operator=(GuestArena&&) = delete;

    // Size must be a positive multiple of 4096; preferred 0 selects
    // deterministic first-fit. Flags are 0, R, RW, RX, W, or X; W+X rejects.
    [[nodiscard]] Result Allocate(std::uint64_t size, Access access,
                                  std::uint64_t preferred = 0);
    [[nodiscard]] Error Free(std::uint64_t base) noexcept;
    // Reclaim every region on synthetic process exit or terminal CPU fault;
    // idempotent and safe even when the arena owns no pages.
    void ReleaseAll() noexcept;
    [[nodiscard]] Error Protect(std::uint64_t base, Access access) noexcept;
    // Queries any byte within an allocation, without revealing host mappings.
    [[nodiscard]] Result Query(std::uint64_t address) const noexcept;
    [[nodiscard]] std::uint64_t AllocatedBytes() const noexcept { return allocated_; }
    [[nodiscard]] std::size_t RegionCount() const noexcept { return regions_.size(); }
    [[nodiscard]] std::uint64_t Begin() const noexcept { return base_; }
    [[nodiscard]] std::uint64_t Length() const noexcept { return length_; }

private:
    [[nodiscard]] bool ValidWindow() const noexcept;
    [[nodiscard]] bool InWindow(std::uint64_t address, std::uint64_t size) const noexcept;
    [[nodiscard]] static bool ValidAccess(Access access) noexcept;
    [[nodiscard]] bool Overlaps(std::uint64_t address, std::uint64_t size) const noexcept;
    [[nodiscard]] Result Install(std::uint64_t address, std::uint64_t size, Access access);
    GuestMemory& memory_;
    std::uint64_t base_{};
    std::uint64_t length_{};
    std::uint64_t allocated_{};
    std::map<std::uint64_t, Region> regions_;
};
} // namespace nint2::vm
