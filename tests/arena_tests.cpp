#include "nint2/arena.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define REQUIRE(test) do { if (!(test)) { \
    std::cerr << "FAILED " << #test << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using namespace nint2;
using namespace nint2::vm;
constexpr std::uint64_t kP = GuestMemory::kPageSize;
constexpr Access kRw = Access::Read | Access::Write;
constexpr Access kRx = Access::Read | Access::Execute;

bool Equal(const Region& region, std::uint64_t base, std::uint64_t size,
           Access access) {
    return region.base == base && region.size == size && region.access == access;
}
}

int main() {
    using namespace nint2;
    using namespace nint2::vm;
    // The arena never enlarges its caller's already bounded guest VA space.
    GuestMemory too_small(GuestArena::kBase);
    GuestArena absent(too_small);
    REQUIRE(absent.Allocate(kP, kRw).error == Error::InvalidArgument);
    REQUIRE(absent.RegionCount() == 0);

    GuestMemory space(0x40000000);
    GuestArena arena(space);
    REQUIRE(arena.RegionCount() == 0);
    REQUIRE(arena.AllocatedBytes() == 0);
    REQUIRE(!space.Read8(GuestArena::kBase).has_value());
    for (const auto size : {std::uint64_t{0}, std::uint64_t{1},
                            kP - 1, kP + 1, GuestArena::kMaxAllocation + kP})
        REQUIRE(arena.Allocate(size, kRw).error == Error::InvalidArgument);
    REQUIRE(arena.Allocate(kP, kRw | Access::Execute).error == Error::InvalidArgument);
    REQUIRE(arena.Allocate(kP, static_cast<Access>(128)).error == Error::InvalidArgument);
    REQUIRE(arena.RegionCount() == 0);
    REQUIRE(arena.Query(GuestArena::kBase).error == Error::InvalidRegion);

    const auto a = arena.Allocate(2*kP, kRw);
    REQUIRE(a.Ok());
    REQUIRE(Equal(a.region, GuestArena::kBase, 2*kP, kRw));
    REQUIRE(arena.RegionCount() == 1 && arena.AllocatedBytes() == 2*kP);
    REQUIRE(space.Read64(a.region.base) == 0);
    REQUIRE(space.Read64(a.region.base + 2*kP - 8) == 0);
    REQUIRE(space.Write32(a.region.base, 0xd503201f));
    REQUIRE(space.Write32(a.region.base + kP, 0xd4200000));
    REQUIRE(space.Read32(a.region.base) == 0xd503201f);
    REQUIRE(arena.Query(a.region.base).Ok());
    REQUIRE(Equal(arena.Query(a.region.base + 2*kP - 1).region,
                  a.region.base, 2*kP, kRw));
    REQUIRE(arena.Query(a.region.base + 2*kP).error == Error::InvalidRegion);
    REQUIRE(arena.Free(a.region.base + kP) == Error::InvalidRegion);
    REQUIRE(arena.Free(0) == Error::InvalidRegion);
    REQUIRE(arena.Protect(a.region.base + kP, kRx) == Error::InvalidRegion);
    REQUIRE(arena.Protect(a.region.base, kRw | Access::Execute) ==
            Error::InvalidArgument);
    REQUIRE(space.Read32(a.region.base) == 0xd503201f);

    // One allocation protects all its pages in a single checked operation.
    REQUIRE(arena.Protect(a.region.base, kRx) == Error::None);
    REQUIRE(!space.Write8(a.region.base, 5));
    REQUIRE(!space.Write8(a.region.base + kP, 5));
    REQUIRE(space.Fetch32(a.region.base) == 0xd503201f);
    REQUIRE(space.Fetch32(a.region.base + kP) == 0xd4200000);
    REQUIRE(arena.Query(a.region.base + kP).region.access == kRx);
    REQUIRE(arena.Protect(a.region.base, kRw) == Error::None);
    REQUIRE(!space.Fetch32(a.region.base).has_value());
    REQUIRE(space.Write8(a.region.base, 7));
    REQUIRE(arena.Query(a.region.base).region.access == kRw);

    // Deterministic first fit and precise collision detection.
    const auto b = arena.Allocate(kP, Access::None);
    REQUIRE(b.Ok() && b.region.base == GuestArena::kBase + 2*kP);
    REQUIRE(!space.Read8(b.region.base).has_value());
    REQUIRE(!space.Write8(b.region.base, 1));
    REQUIRE(arena.Protect(b.region.base, kRw) == Error::None);
    REQUIRE(space.Write8(b.region.base, 0xaa));
    REQUIRE(arena.Allocate(kP, kRw, b.region.base).error == Error::AddressInUse);
    REQUIRE(arena.Allocate(kP, kRw, GuestArena::kBase + 1).error ==
            Error::InvalidArgument);
    REQUIRE(arena.Allocate(kP, kRw, GuestArena::kBase - kP).error ==
            Error::InvalidArgument);
    REQUIRE(arena.Allocate(2*kP, kRw, GuestArena::kBase + GuestArena::kLength - kP)
                .error == Error::InvalidArgument);
    REQUIRE(arena.Allocate(kP, kRw, 0x30000000).error ==
            Error::InvalidArgument);
    REQUIRE(arena.RegionCount() == 2);

    REQUIRE(arena.Free(a.region.base) == Error::None);
    REQUIRE(arena.AllocatedBytes() == kP);
    REQUIRE(!space.Read8(a.region.base).has_value());
    REQUIRE(!space.Fetch32(a.region.base).has_value());
    REQUIRE(arena.Free(a.region.base) == Error::InvalidRegion);
    const auto reused = arena.Allocate(2*kP, kRw);
    REQUIRE(reused.Ok() && reused.region.base == a.region.base);
    REQUIRE(space.Read32(a.region.base) == 0); // mapping never leaks stale data
    REQUIRE(arena.Free(reused.region.base) == Error::None);
    REQUIRE(arena.Free(b.region.base) == Error::None);
    REQUIRE(arena.AllocatedBytes() == 0 && arena.RegionCount() == 0);

    // Host mapped memory in the arena aperture is not owned by this arena.
    const auto host_page = GuestArena::kBase;
    REQUIRE(space.Map(host_page, kP, kRw));
    REQUIRE(space.Write8(host_page, 'H'));
    REQUIRE(arena.Allocate(kP, kRw, host_page).error == Error::MappingFailed);
    const auto skipped = arena.Allocate(kP, kRw);
    REQUIRE(skipped.Ok() && skipped.region.base == host_page + kP);
    REQUIRE(space.Read8(host_page) == 'H');
    REQUIRE(arena.Query(host_page).error == Error::InvalidRegion);
    REQUIRE(arena.Free(host_page) == Error::InvalidRegion);
    REQUIRE(arena.Free(skipped.region.base) == Error::None);
    REQUIRE(space.Unmap(host_page, kP));

    // W^X is global to aliased frames, not just the originating mapping.
    const auto mirrored = arena.Allocate(kP, kRw);
    REQUIRE(mirrored.Ok());
    REQUIRE(space.Alias(0x30000000, mirrored.region.base, kP, kRw));
    REQUIRE(arena.Protect(mirrored.region.base, kRx) == Error::MappingFailed);
    REQUIRE(arena.Query(mirrored.region.base).region.access == kRw);
    REQUIRE(space.Write8(mirrored.region.base, 0x77));
    REQUIRE(space.Read8(0x30000000) == 0x77);
    REQUIRE(space.Unmap(0x30000000, kP));
    REQUIRE(arena.Protect(mirrored.region.base, kRx) == Error::None);
    REQUIRE(arena.Free(mirrored.region.base) == Error::None);

    // Writable-only permissions can be tightened to readable-only.
    const auto write_only = arena.Allocate(kP, Access::Write);
    REQUIRE(write_only.Ok());
    REQUIRE(space.Write8(write_only.region.base, 0x6c));
    REQUIRE(!space.Read8(write_only.region.base));
    REQUIRE(arena.Protect(write_only.region.base, Access::Read) == Error::None);
    REQUIRE(space.Read8(write_only.region.base) == 0x6c);
    REQUIRE(!space.Write8(write_only.region.base, 0x6d));
    REQUIRE(arena.Free(write_only.region.base) == Error::None);

    // Invalid windows and allocations larger than the window are bounded.
    GuestMemory tiny_space(0x10000);
    GuestArena tiny(tiny_space, 0x2000, 3*kP);
    REQUIRE(tiny.Allocate(4*kP, kRw).error == Error::InvalidArgument);
    const auto constrained = tiny.Allocate(2*kP, kRw);
    REQUIRE(constrained.Ok() && constrained.region.base == 0x2000);
    REQUIRE(tiny.Allocate(2*kP, kRw).error == Error::ResourceLimit);
    REQUIRE(tiny.Free(0x2000) == Error::None);
    GuestArena bad_window(tiny_space, 0x2001, 3*kP);
    REQUIRE(bad_window.Allocate(kP, kRw).error == Error::InvalidArgument);

    // Total-mapped accounting is independent of huge guest VA aperture.
    GuestMemory quota_space(0x40000000);
    GuestArena quota(quota_space);
    std::array<std::uint64_t, 4> large{};
    for (std::size_t i = 0; i < large.size(); ++i) {
        const auto block = quota.Allocate(GuestArena::kMaxAllocation, kRw);
        REQUIRE(block.Ok());
        large[i] = block.region.base;
        REQUIRE(quota.AllocatedBytes() == (i + 1)*GuestArena::kMaxAllocation);
    }
    REQUIRE(quota.AllocatedBytes() == GuestArena::kMaxMappedBytes);
    REQUIRE(quota.Allocate(kP, kRw).error == Error::ResourceLimit);
    REQUIRE(quota.Free(large[1]) == Error::None);
    REQUIRE(quota.AllocatedBytes() == 3*GuestArena::kMaxAllocation);
    const auto recovered = quota.Allocate(kP, kRw);
    REQUIRE(recovered.Ok() && recovered.region.base == large[1]);

    // The number of regions is capped even for zero-filled sparse mappings.
    GuestMemory handle_space(0x40000000);
    GuestArena count(handle_space);
    std::vector<std::uint64_t> locations;
    for (std::size_t i = 0; i < GuestArena::kMaxRegions; ++i) {
        const auto block = count.Allocate(kP, kRw);
        REQUIRE(block.Ok());
        locations.push_back(block.region.base);
    }
    REQUIRE(count.RegionCount() == GuestArena::kMaxRegions);
    REQUIRE(count.Allocate(kP, kRw).error == Error::ResourceLimit);
    REQUIRE(count.Free(locations[47]) == Error::None);
    REQUIRE(count.RegionCount() == GuestArena::kMaxRegions - 1);
    const auto reuse_slot = count.Allocate(kP, kRw);
    REQUIRE(reuse_slot.Ok() && reuse_slot.region.base == locations[47]);
    REQUIRE(count.RegionCount() == GuestArena::kMaxRegions);

    std::cout << "Guest virtual arena: ownership, quota, W^X, aliases, first-fit and zeroing passed\n";
    return EXIT_SUCCESS;
}
