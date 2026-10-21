#include <cstdlib>
#include <iostream>
#include "nint2/memory.hpp"

#define EXPECT(condition)                                                        \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::cerr << "FAILED: " << #condition << " line " << __LINE__ << '\n'; \
            return EXIT_FAILURE;                                                 \
        }                                                                        \
    } while (false)

int main() {
    using nint2::Access;
    using nint2::GuestMemory;
    GuestMemory memory(0x10000);
    EXPECT(!memory.Write8(0x1000, 1));
    EXPECT(!memory.Read8(0x1000).has_value());
    EXPECT(!memory.Map(0x1001, GuestMemory::kPageSize, Access::Read));
    EXPECT(!memory.Map(0xF000, 2 * GuestMemory::kPageSize, Access::Read));
    EXPECT(!memory.Map(0x1000, GuestMemory::kPageSize, Access::Write | Access::Execute));
    EXPECT(memory.Map(0x1000, 2 * GuestMemory::kPageSize, Access::Read | Access::Write));
    EXPECT(!memory.Map(0x1000, GuestMemory::kPageSize, Access::Read));
    EXPECT(memory.Write8(0x1000, 42));
    EXPECT(memory.Write8(0x2fff, 99));
    EXPECT(memory.Read8(0x1000) == 42);
    EXPECT(memory.Read8(0x2fff) == 99);
    EXPECT(memory.Read64(0x1000) == 42);
    EXPECT(memory.Write64(0x1ffc, 0x1122334455667788ULL));
    EXPECT(memory.Read64(0x1ffc) == 0x1122334455667788ULL);
    EXPECT(memory.Read32(0x2000) == 0x11223344U);
    EXPECT(!memory.Write32(0x2ffe, 1));
    EXPECT(memory.Alias(0x4000, 0x1000, GuestMemory::kPageSize, Access::Read | Access::Write));
    EXPECT(memory.Read8(0x4000) == 42);
    EXPECT(memory.Write8(0x4000, 77));
    EXPECT(memory.Read8(0x1000) == 77);
    EXPECT(memory.Map(0x6000, GuestMemory::kPageSize, Access::Read));
    EXPECT(memory.Alias(0x7000, 0x6000, GuestMemory::kPageSize, Access::Read | Access::Write));
    EXPECT(memory.Write8(0x7000, 98));
    EXPECT(memory.Read8(0x6000) == 98);
    EXPECT(!memory.Protect(0x1000, GuestMemory::kPageSize, Access::Read | Access::Execute));
    EXPECT(memory.Unmap(0x4000, GuestMemory::kPageSize));
    EXPECT(memory.Protect(0x1000, GuestMemory::kPageSize, Access::Read | Access::Execute));
    EXPECT(!memory.Write8(0x1000, 21));
    EXPECT(!memory.Alias(0x8000, 0x1000, GuestMemory::kPageSize, Access::Write));
    EXPECT(memory.Read8(0x1000) == 77);
    EXPECT(memory.Fetch32(0x1000) == memory.Read32(0x1000));
    EXPECT(!memory.Fetch32(0x1001).has_value());
    EXPECT(!memory.Fetch32(0x2000).has_value());
    EXPECT(!memory.Unmap(0x3000, GuestMemory::kPageSize));
    EXPECT(memory.Unmap(0x1000, 2 * GuestMemory::kPageSize));
    EXPECT(!memory.Read8(0x1000).has_value());
    GuestMemory sparse(0x100000000ULL);
    EXPECT(sparse.Map(0xFFF00000ULL, GuestMemory::kPageSize, Access::Read | Access::Write));
    EXPECT(sparse.Read64(0xFFF00000ULL) == 0);
    EXPECT(sparse.Write16(0xFFF00FFEULL, 0xbeef));
    EXPECT(sparse.Read16(0xFFF00FFEULL) == 0xbeef);
    EXPECT(!sparse.Read64(0xFFFFFFFFULL).has_value());
    EXPECT(!sparse.Write32(UINT64_MAX - 1, 1));
    std::cout << "GuestMemory mapping/permissions/aliases/typed access tests passed\n";
    return EXIT_SUCCESS;
}
