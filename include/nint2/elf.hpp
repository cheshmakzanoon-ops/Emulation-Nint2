#pragma once
#include <cstdint>
#include <span>
#include "nint2/memory.hpp"
namespace nint2 {
enum class ElfError : std::uint8_t {
    None, InvalidHeader, UnsupportedFormat, InvalidSegment,
    ResourceLimit, NoExecutableEntry, MappingFailed,
    InvalidRelocation, UnsupportedRelocation
};
struct ElfResult {
    ElfError error{ElfError::InvalidHeader};
    std::uint64_t entry{};
    std::uint64_t load_bias{};
};
// ET_EXEC and static freestanding ET_DYN only. No dynamic symbol linking.
// The chosen ET_DYN lowest guest page is deterministic, never host memory.
[[nodiscard]] ElfResult LoadElf64(std::span<const std::uint8_t> image,
                                   GuestMemory& memory,
                                   std::uint64_t pie_base = 0x200000);
[[nodiscard]] const char* ElfErrorName(ElfError error) noexcept;
} // namespace nint2
