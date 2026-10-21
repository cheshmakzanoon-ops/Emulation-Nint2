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
    EXPECT(memory.Protect(0x1000, GuestMemory::kPageSize, Access::Read | Access::Execute));
    EXPECT(!memory.Write8(0x1000, 21));
    EXPECT(memory.Read8(0x1000) == 42);
    EXPECT(!memory.Unmap(0x3000, GuestMemory::kPageSize));
    EXPECT(memory.Unmap(0x1000, 2 * GuestMemory::kPageSize));
    EXPECT(!memory.Read8(0x1000).has_value());
    std::cout << "GuestMemory mapping/permissions tests passed\n";
    return EXIT_SUCCESS;
}
