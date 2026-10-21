#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

// Page metadata is sparse; page data is allocated only on the first successful write.
// Page aliases share a frame, including backing allocated after the alias is created.
class GuestMemory final {
public:
    static constexpr std::uint64_t kPageSize = 4096;
    explicit GuestMemory(std::uint64_t address_limit) noexcept;
    [[nodiscard]] std::uint64_t AddressLimit() const noexcept { return address_limit_; }
    [[nodiscard]] bool Map(std::uint64_t base, std::uint64_t size, Access access);
    [[nodiscard]] bool Alias(std::uint64_t dst, std::uint64_t src, std::uint64_t size, Access access);
    [[nodiscard]] bool Unmap(std::uint64_t base, std::uint64_t size);
    [[nodiscard]] bool Protect(std::uint64_t base, std::uint64_t size, Access access);
    [[nodiscard]] std::optional<std::uint8_t> Read8(std::uint64_t address) const;
    [[nodiscard]] std::optional<std::uint16_t> Read16(std::uint64_t address) const;
    [[nodiscard]] std::optional<std::uint32_t> Read32(std::uint64_t address) const;
    [[nodiscard]] std::optional<std::uint64_t> Read64(std::uint64_t address) const;
    [[nodiscard]] std::optional<std::uint32_t> Fetch32(std::uint64_t pc) const;
    [[nodiscard]] bool Write8(std::uint64_t address, std::uint8_t value);
    [[nodiscard]] bool Write16(std::uint64_t address, std::uint16_t value);
    [[nodiscard]] bool Write32(std::uint64_t address, std::uint32_t value);
    [[nodiscard]] bool Write64(std::uint64_t address, std::uint64_t value);
    // All-or-nothing guest writes: validate all bytes and allocate all backing
    // pages before modifying data. Allows IPC receive without partial copying.
    [[nodiscard]] bool WriteBuffer(std::uint64_t address, std::span<const std::uint8_t> data);

    // This synthetic reference engine uses one host execution thread.
    // An exclusive stamp names the physical frame (so aliases share its
    // monitor state) and a per-frame mutation generation. Guest pointers
    // never escape into native host addresses.
    struct ExclusiveRead {
        std::uint64_t value{};
        std::uint64_t frame_id{};
        std::uint64_t generation{};
    };
    enum class ExclusiveStore : std::uint8_t { Stored, ReservationLost, Fault };
    // Architecturally atomic LSE read/modify/write operations. All validations
    // and the memory mutation occur on the one deterministic host CPU thread.
    enum class AtomicOperation : std::uint8_t {
        Swap, Add, Clear, Xor, Set, SignedMax, SignedMin,
        UnsignedMax, UnsignedMin, CompareSwap
    };
    // The result is the previous (zero-extended) value, including CAS mismatch.
    // Nullopt means a data-access fault; neither bytes nor generation change.
    // CAS mismatch does not modify bytes or invalidate existing reservations.
    [[nodiscard]] std::optional<std::uint64_t> AtomicRmw(
        std::uint64_t address, unsigned bytes, AtomicOperation operation,
        std::uint64_t operand, std::uint64_t expected = 0);
    [[nodiscard]] std::optional<ExclusiveRead>
    LoadExclusive(std::uint64_t address, unsigned bytes) const;
    [[nodiscard]] ExclusiveStore StoreExclusive(
        std::uint64_t address, unsigned bytes, std::uint64_t value,
        std::uint64_t frame_id, std::uint64_t generation);

private:
    struct Frame {
        std::unique_ptr<std::array<std::uint8_t, kPageSize>> data;
        std::uint64_t id{};
        std::uint64_t generation{};
    };
    struct Page {
        std::shared_ptr<Frame> frame;
        Access access{Access::None};
    };
    [[nodiscard]] bool ValidPageRange(std::uint64_t base, std::uint64_t size) const noexcept;
    [[nodiscard]] static bool SecureAccess(Access access) noexcept;
    [[nodiscard]] bool FrameAccessAllowed(const std::shared_ptr<Frame>& frame, Access access,
                                          std::uint64_t ignore_base = 0,
                                          std::uint64_t ignore_size = 0) const;
    [[nodiscard]] std::optional<std::uint64_t> ReadLe(std::uint64_t address,
                                                      unsigned bytes, Access access) const;
    [[nodiscard]] bool WriteLe(std::uint64_t address, std::uint64_t value, unsigned bytes);
    std::uint64_t address_limit_;
    std::uint64_t next_frame_id_{1};
    std::unordered_map<std::uint64_t, Page> pages_;
};
} // namespace nint2
