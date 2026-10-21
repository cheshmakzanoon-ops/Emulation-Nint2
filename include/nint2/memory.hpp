#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace nint2 {
enum class Access : std::uint8_t { None = 0, Read = 1, Write = 2, Execute = 4 };

constexpr Access operator|(Access lhs, Access rhs) noexcept {
    return static_cast<Access>(static_cast<std::uint8_t>(lhs) |
                               static_cast<std::uint8_t>(rhs));
}
constexpr bool HasAccess(Access value, Access required) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(required)) ==
           static_cast<std::uint8_t>(required);
}

// M0 bootstrap: eager per-mapped-page backing; later phases add sparse/lazy backing.
class GuestMemory final {
public:
    static constexpr std::uint64_t kPageSize = 4096;
    explicit GuestMemory(std::uint64_t address_limit) noexcept;
    [[nodiscard]] bool Map(std::uint64_t base, std::uint64_t size, Access access);
    [[nodiscard]] bool Unmap(std::uint64_t base, std::uint64_t size);
    [[nodiscard]] bool Protect(std::uint64_t base, std::uint64_t size, Access access);
    [[nodiscard]] std::optional<std::uint8_t> Read8(std::uint64_t address) const;
    [[nodiscard]] bool Write8(std::uint64_t address, std::uint8_t value);
private:
    struct Page {
        std::array<std::uint8_t, kPageSize> data{};
        Access access{Access::None};
    };
    [[nodiscard]] bool ValidPageRange(std::uint64_t base, std::uint64_t size) const noexcept;
    [[nodiscard]] static bool SecureAccess(Access access) noexcept;
    std::uint64_t address_limit_;
    std::unordered_map<std::uint64_t, Page> pages_;
};
} // namespace nint2
