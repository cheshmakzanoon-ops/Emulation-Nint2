#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/memory.hpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "Guest atomic memory FAILED " #x << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::GuestMemory;
using nint2::Access;
using Operation = GuestMemory::AtomicOperation;
constexpr std::array<Operation, 10> kOperations{
    Operation::Swap, Operation::Add, Operation::Clear, Operation::Xor,
    Operation::Set, Operation::SignedMax, Operation::SignedMin,
    Operation::UnsignedMax, Operation::UnsignedMin, Operation::CompareSwap
};
std::uint64_t Mask(unsigned bytes) {
    return bytes == 8 ? UINT64_MAX : (std::uint64_t{1} << (8 * bytes)) - 1;
}
bool Put(GuestMemory& m, std::uint64_t at, unsigned bytes, std::uint64_t value) {
    switch (bytes) {
    case 1: return m.Write8(at, static_cast<std::uint8_t>(value));
    case 2: return m.Write16(at, static_cast<std::uint16_t>(value));
    case 4: return m.Write32(at, static_cast<std::uint32_t>(value));
    case 8: return m.Write64(at, value);
    default: return false;
    }
}
std::uint64_t Get(const GuestMemory& m, std::uint64_t at, unsigned bytes) {
    switch (bytes) {
    case 1: return m.Read8(at).value();
    case 2: return m.Read16(at).value();
    case 4: return m.Read32(at).value();
    case 8: return m.Read64(at).value();
    default: return UINT64_MAX;
    }
}
std::uint64_t Expected(Operation op, std::uint64_t old, std::uint64_t input,
                       std::uint64_t expected, unsigned bytes) {
    const auto mask = Mask(bytes);
    const auto sign = std::uint64_t{1} << (bytes * 8 - 1);
    old &= mask;
    input &= mask;
    switch (op) {
    case Operation::Swap: return input;
    case Operation::Add: return (old + input) & mask;
    case Operation::Clear: return old & ~input & mask;
    case Operation::Xor: return (old ^ input) & mask;
    case Operation::Set: return (old | input) & mask;
    case Operation::SignedMax:
        return (old ^ sign) >= (input ^ sign) ? old : input;
    case Operation::SignedMin:
        return (old ^ sign) <= (input ^ sign) ? old : input;
    case Operation::UnsignedMax: return old >= input ? old : input;
    case Operation::UnsignedMin: return old <= input ? old : input;
    case Operation::CompareSwap: return old == (expected & mask) ? input : old;
    }
    return UINT64_MAX;
}
} // namespace

int main() {
    // Each valid access size exercises the same original reference model and
    // randomized operands. The backing page is sparse until first mutation.
    std::uint32_t rng = 0x9b4302f1U;
    for (unsigned bytes : {1U, 2U, 4U, 8U}) {
        for (Operation op : kOperations) {
            for (unsigned trial = 0; trial < 128; ++trial) {
                GuestMemory memory(0x10000);
                CHECK(memory.Map(0x3000, 0x1000, Access::Read | Access::Write));
                CHECK(memory.Alias(0x5000, 0x3000, 0x1000,
                                   Access::Read | Access::Write));
                rng = rng * 1664525U + 1013904223U;
                const auto initial = (std::uint64_t{rng} << 23) ^
                                     std::uint64_t{rng * 22695477U};
                rng = rng * 1664525U + 1013904223U;
                const auto rhs = (std::uint64_t{rng} << 11) ^
                                 std::uint64_t{rng * 747796405U};
                const auto mask = Mask(bytes);
                CHECK(Put(memory, 0x3000, bytes, initial));
                const auto before = initial & mask;
                const auto expected = trial % 2 == 0 ? before : before ^ 1U;
                const auto expected_result =
                    Expected(op, before, rhs, expected, bytes);
                const auto returned =
                    memory.AtomicRmw(0x5000, bytes, op, rhs, expected);
                CHECK(returned == before);
                CHECK(Get(memory, 0x3000, bytes) == expected_result);
                CHECK(Get(memory, 0x5000, bytes) == expected_result);
            }
        }
    }

    // Critical signed boundary behavior: the unsigned representation of -1
    // must lose to zero in signed max but win in unsigned max for all widths.
    for (unsigned bytes : {1U, 2U, 4U, 8U}) {
        GuestMemory m(0x6000);
        CHECK(m.Map(0x2000, 0x1000, Access::Read | Access::Write));
        CHECK(Put(m, 0x2000, bytes, Mask(bytes)));
        CHECK(m.AtomicRmw(0x2000, bytes, Operation::SignedMax, 0) == Mask(bytes));
        CHECK(Get(m, 0x2000, bytes) == 0);
        CHECK(m.AtomicRmw(0x2000, bytes, Operation::SignedMin, Mask(bytes)) == 0);
        CHECK(Get(m, 0x2000, bytes) == Mask(bytes));
        CHECK(m.AtomicRmw(0x2000, bytes, Operation::UnsignedMax, 0) == Mask(bytes));
        CHECK(Get(m, 0x2000, bytes) == Mask(bytes));
        CHECK(m.AtomicRmw(0x2000, bytes, Operation::UnsignedMin, 0) == Mask(bytes));
        CHECK(Get(m, 0x2000, bytes) == 0);
    }

    // Compare-and-swap mismatch must not invalidate an exclusive reservation.
    // Successful CAS and even equal-value SWP must invalidate it across aliases.
    {
        GuestMemory m(0x10000);
        CHECK(m.Map(0x3000, 0x1000, Access::Read | Access::Write));
        CHECK(m.Alias(0x4000, 0x3000, 0x1000, Access::Read | Access::Write));
        CHECK(m.Write64(0x3000, 8));
        const auto a = m.LoadExclusive(0x3000, 8);
        CHECK(a.has_value());
        CHECK(m.AtomicRmw(0x4000, 8, Operation::CompareSwap, 9, 7) == 8);
        CHECK(m.Read64(0x3000) == 8);
        CHECK(m.StoreExclusive(0x3000, 8, 8, a->frame_id, a->generation) ==
              GuestMemory::ExclusiveStore::Stored);
        const auto b = m.LoadExclusive(0x3000, 8);
        CHECK(b.has_value());
        CHECK(m.AtomicRmw(0x4000, 8, Operation::Swap, 8) == 8);
        CHECK(m.StoreExclusive(0x3000, 8, 9, b->frame_id, b->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
        const auto c = m.LoadExclusive(0x3000, 8);
        CHECK(c.has_value());
        CHECK(m.AtomicRmw(0x4000, 8, Operation::CompareSwap, 9, 8) == 8);
        CHECK(m.Read64(0x3000) == 9);
        CHECK(m.StoreExclusive(0x3000, 8, 10, c->frame_id, c->generation) ==
              GuestMemory::ExclusiveStore::ReservationLost);
    }

    // Natural alignment, unmapped memory, inaccessible pages and invalid
    // access sizes must fault without touching surrounding memory.
    {
        GuestMemory m(0x6000);
        CHECK(m.Map(0x2000, 0x1000, Access::Read | Access::Write));
        CHECK(m.Write64(0x2000, 0x1122334455667788ULL));
        for (unsigned bytes : {0U, 3U, 7U, 9U})
            CHECK(!m.AtomicRmw(0x2000, bytes, Operation::Swap, 5));
        for (unsigned bytes : {2U, 4U, 8U})
            CHECK(!m.AtomicRmw(0x2001, bytes, Operation::Add, 5));
        CHECK(!m.AtomicRmw(0x3000, 8, Operation::Swap, 5));
        CHECK(!m.AtomicRmw(UINT64_MAX - 1, 8, Operation::Add, 5));
        CHECK(m.Read64(0x2000) == 0x1122334455667788ULL);
        CHECK(m.Protect(0x2000, 0x1000, Access::Read));
        CHECK(!m.AtomicRmw(0x2000, 8, Operation::CompareSwap, 5, 0));
        CHECK(m.Read64(0x2000) == 0x1122334455667788ULL);
        CHECK(m.Protect(0x2000, 0x1000, Access::Write));
        CHECK(!m.AtomicRmw(0x2000, 8, Operation::CompareSwap, 5, 0));
        CHECK(m.Protect(0x2000, 0x1000, Access::Read | Access::Write));
        CHECK(m.Read64(0x2000) == 0x1122334455667788ULL);
    }

    std::cout << "Guest memory atomics: 5120 deterministic operations,"
                 " signed limits, aliases, monitor generations and faults passed\n";
    return EXIT_SUCCESS;
}
