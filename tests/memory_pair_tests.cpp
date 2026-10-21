#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include "nint2/memory.hpp"

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "Guest atomic-pair failure: " #expr \
              << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::Access;
using nint2::GuestMemory;
std::uint64_t Mask(unsigned width) {
    return width == 8 ? UINT64_MAX : UINT64_C(0xffffffff);
}
bool Put(GuestMemory& m, std::uint64_t a, unsigned width, std::uint64_t v) {
    if (width == 8) return m.Write64(a, v);
    return m.Write32(a, static_cast<std::uint32_t>(v));
}
std::optional<std::uint64_t> Get(const GuestMemory& m,
                                 std::uint64_t a, unsigned width) {
    if (width == 8) return m.Read64(a);
    return m.Read32(a);
}
} // namespace

int main() {
    // 2 widths * 1024 deterministic source-value pairs * two CAS outcomes.
    // Alias reads must always be coherent; failed compares must not write.
    std::uint32_t seed = 0x2468a15bU;
    std::uint64_t cases = 0;
    for (unsigned width : {4U, 8U}) {
        for (unsigned i = 0; i < 1024; ++i) {
            GuestMemory m(0x10000);
            CHECK(m.Map(0x3000, 4096, Access::Read | Access::Write));
            CHECK(m.Alias(0x5000, 0x3000, 4096, Access::Read | Access::Write));
            seed = seed * 1664525U + 1013904223U;
            const auto lo = (std::uint64_t{seed} << 29) ^
                            std::uint64_t{seed * 747796405U};
            seed = seed * 1664525U + 1013904223U;
            const auto hi = (std::uint64_t{seed} << 11) ^
                            std::uint64_t{seed * 22695477U};
            seed = seed * 1664525U + 1013904223U;
            const auto next_lo = (std::uint64_t{seed} << 23) ^ seed;
            seed = seed * 1664525U + 1013904223U;
            const auto next_hi = (std::uint64_t{seed} << 37) ^ seed;
            const auto mask = Mask(width);
            CHECK(Put(m, 0x3000, width, lo));
            CHECK(Put(m, 0x3000 + width, width, hi));
            const auto previous = m.LoadExclusivePair(0x3000, width);
            CHECK(previous.has_value());
            CHECK(previous->first == (lo & mask));
            CHECK(previous->second == (hi & mask));
            CHECK(previous->frame_id != 0);
            CHECK(previous->generation >= 2);

            // CASP mismatch must return BOTH original registers unchanged and
            // preserve the monitor. Even matching one word is insufficient.
            const auto mismatch = m.AtomicComparePair(
                0x5000, width, lo, hi ^ 1, next_lo, next_hi);
            CHECK(mismatch.has_value());
            CHECK(mismatch->first == (lo & mask));
            CHECK(mismatch->second == (hi & mask));
            CHECK(Get(m, 0x3000, width) == (lo & mask));
            CHECK(Get(m, 0x3000 + width, width) == (hi & mask));
            const auto would_succeed = m.StoreExclusivePair(
                0x3000, width, lo, hi, previous->frame_id,
                previous->generation);
            CHECK(would_succeed == GuestMemory::ExclusiveStore::Stored);

            const auto reservation = m.LoadExclusivePair(0x5000, width);
            CHECK(reservation.has_value());
            const auto matched = m.AtomicComparePair(
                0x3000, width, lo, hi, next_lo, next_hi);
            CHECK(matched.has_value());
            CHECK(matched->first == (lo & mask));
            CHECK(matched->second == (hi & mask));
            CHECK(Get(m, 0x3000, width) == (next_lo & mask));
            CHECK(Get(m, 0x5000 + width, width) == (next_hi & mask));
            CHECK(m.StoreExclusivePair(
                0x5000, width, 0, 0, reservation->frame_id,
                reservation->generation) ==
                  GuestMemory::ExclusiveStore::ReservationLost);
            cases += 2;
        }
    }

    // A fresh sparse page must initially present zeros to atomics and store
    // both pair elements in one operation, not leave half of the pair.
    for (unsigned width : {4U, 8U}) {
        GuestMemory m(0x7000);
        CHECK(m.Map(0x3000, 4096, Access::Read | Access::Write));
        const auto observed = m.LoadExclusivePair(0x3000, width);
        CHECK(observed.has_value());
        CHECK(observed->first == 0 && observed->second == 0);
        CHECK(m.StoreExclusivePair(
            0x3000, width, UINT64_MAX, UINT64_MAX,
            observed->frame_id, observed->generation) ==
              GuestMemory::ExclusiveStore::Stored);
        CHECK(Get(m, 0x3000, width) == Mask(width));
        CHECK(Get(m, 0x3000 + width, width) == Mask(width));
        // An aligned terminal pair ending precisely at a page boundary is
        // valid. Adjacent unmapped pages must not be read or changed.
        const auto end = 0x4000 - 2 * width;
        const auto before = m.LoadExclusivePair(end, width);
        CHECK(before.has_value());
        CHECK(before->first == 0 && before->second == 0);
        CHECK(m.StoreExclusivePair(
            end, width, 11, 22, before->frame_id, before->generation) ==
              GuestMemory::ExclusiveStore::Stored);
        CHECK(Get(m, end, width) == 11);
        CHECK(Get(m, end + width, width) == 22);
        CHECK(!m.LoadExclusivePair(0x4000, width).has_value());
        CHECK(!m.AtomicComparePair(0x4000, width, 0, 0, 1, 2).has_value());
    }

    // Two separate exclusive reservations on aliases have shared physical
    // generation stamps. Writing any byte of either word invalidates both.
    {
        GuestMemory m(0x8000);
        CHECK(m.Map(0x3000, 4096, Access::Read | Access::Write));
        CHECK(m.Alias(0x5000, 0x3000, 4096, Access::Read | Access::Write));
        CHECK(m.Write64(0x3000, 101));
        CHECK(m.Write64(0x3008, 202));
        const auto a = m.LoadExclusivePair(0x3000, 8);
        const auto b = m.LoadExclusivePair(0x5000, 8);
        CHECK(a.has_value() && b.has_value());
        CHECK(a->frame_id == b->frame_id && a->generation == b->generation);
        CHECK(m.Write8(0x500f, 0xf0));
        CHECK(m.StoreExclusivePair(0x3000, 8, 333, 444,
              a->frame_id, a->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
        CHECK(m.StoreExclusivePair(0x5000, 8, 333, 444,
              b->frame_id, b->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
    }

    // A successful same-value compare-and-swap remains a memory write and
    // must invalidate the older exclusive pair monitor.
    {
        GuestMemory m(0x8000);
        CHECK(m.Map(0x3000, 4096, Access::Read | Access::Write));
        CHECK(m.Write64(0x3000, 71));
        CHECK(m.Write64(0x3008, 73));
        const auto a = m.LoadExclusivePair(0x3000, 8);
        CHECK(a.has_value());
        const auto result = m.AtomicComparePair(0x3000, 8, 71, 73, 71, 73);
        CHECK(result.has_value() && result->first == 71 && result->second == 73);
        CHECK(m.StoreExclusivePair(0x3000, 8, 1, 1, a->frame_id, a->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
    }

    // Invalid sizes, misalignment, missing permissions, and overflows fault
    // without corrupting sentinels or mutating page contents.
    for (unsigned width : {4U, 8U}) {
        GuestMemory m(0x8000);
        CHECK(m.Map(0x3000, 4096, Access::Read | Access::Write));
        CHECK(m.Write64(0x3000, 0x1122334455667788ULL));
        const auto sentinel = m.Read64(0x3000);
        CHECK(!m.LoadExclusivePair(0x3000 + width, width));
        CHECK(!m.AtomicComparePair(0x3000 + width, width, 0, 0, 1, 2));
        CHECK(m.StoreExclusivePair(0x3000 + width, width, 1, 2, 1, 1) ==
              GuestMemory::ExclusiveStore::Fault);
        CHECK(!m.LoadExclusivePair(UINT64_MAX, width));
        CHECK(!m.AtomicComparePair(UINT64_MAX, width, 0, 0, 1, 2));
        CHECK(m.StoreExclusivePair(UINT64_MAX, width, 1, 2, 1, 1) ==
              GuestMemory::ExclusiveStore::Fault);
        CHECK(!m.LoadExclusivePair(0x3000, 3));
        CHECK(!m.AtomicComparePair(0x3000, 6, 0, 0, 1, 2));
        CHECK(m.StoreExclusivePair(0x3000, 7, 0, 0, 1, 1) ==
              GuestMemory::ExclusiveStore::Fault);
        CHECK(m.Read64(0x3000) == sentinel);
        CHECK(m.Protect(0x3000, 4096, Access::Read));
        CHECK(m.LoadExclusivePair(0x3000, width).has_value());
        CHECK(!m.AtomicComparePair(0x3000, width, 0, 0, 1, 2));
        const auto read_stamp = m.LoadExclusivePair(0x3000, width);
        CHECK(read_stamp.has_value());
        CHECK(m.StoreExclusivePair(0x3000, width, 1, 2,
              read_stamp->frame_id, read_stamp->generation) ==
              GuestMemory::ExclusiveStore::Fault);
        CHECK(m.Protect(0x3000, 4096, Access::Write));
        CHECK(!m.LoadExclusivePair(0x3000, width));
        CHECK(!m.AtomicComparePair(0x3000, width, 0, 0, 1, 2));
        CHECK(m.Protect(0x3000, 4096, Access::Read | Access::Write));
        CHECK(m.Read64(0x3000) == sentinel);
    }

    std::cout << "Guest memory: 4096 independent 64/128-bit CASP outcomes,"
                 " exclusive pair stamps, aliases, sparse pages and faults passed\n";
    return EXIT_SUCCESS;
}
