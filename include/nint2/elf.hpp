#pragma once
#include <cstdint>
#include <span>
#include "nint2/memory.hpp"

namespace nint2 {
enum class ElfError : std::uint8_t {
    None, InvalidHeader, UnsupportedFormat, InvalidSegment,
    ResourceLimit, NoExecutableEntry, MappingFailed
};
struct ElfResult {
    ElfError error{ElfError::InvalidHeader};
    std::uint64_t entry{};
};

// Original-programs-only ELF64 little-endian AArch64 ET_EXEC loader.
// Rejects dynamic linking, overlapping pages, W+X, and oversized segments.
[[nodiscard]] ElfResult LoadElf64(std::span<const std::uint8_t> image,
                                   GuestMemory& memory);
[[nodiscard]] const char* ElfErrorName(ElfError error) noexcept;
} // namespace nint2
