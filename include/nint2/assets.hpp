#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nint2::assets {

// A synthetic, hermetic, read-only asset provider for original guest programs.
// No host paths, filesystem descriptors, or Nintendo platform APIs are exposed.
enum class Error : std::uint8_t {
    None, InvalidPath, Duplicate, NotFound, TooManyAssets, TooLarge,
    NoSpace, InvalidHandle, TooManyHandles, OutOfMemory, InvalidArgument, Sealed
};

struct Result {
    Error error{Error::None};
    std::uint64_t value{};
    [[nodiscard]] constexpr bool Ok() const noexcept { return error == Error::None; }
};

struct Info {
    std::uint64_t size{};
    std::uint64_t digest{}; // deterministic FNV-1a 64 of entire immutable file
    std::uint64_t offset{}; // per-descriptor cursor; zero for path enumeration
};

// All resource accounting is explicit. Mount operations copy the supplied
// bytes; the host can release its buffer as soon as Mount returns.
class AssetStore final {
public:
    static constexpr std::size_t kMaxPath = 128;       // max chars incl leading '/'
    static constexpr std::size_t kMaxAssets = 64;
    static constexpr std::size_t kMaxFileBytes = 1024 * 1024;
    static constexpr std::size_t kMaxTotalBytes = 16 * 1024 * 1024;
    static constexpr std::size_t kMaxHandles = 128;
    static constexpr std::size_t kMaxRead = 4096;
    static constexpr std::uint32_t kFirstHandle = 0x10000U;
    static constexpr std::uint64_t kMaxSeek = std::uint64_t{1} << 40;

    [[nodiscard]] static bool ValidPath(std::string_view path) noexcept;
    [[nodiscard]] static std::uint64_t Hash(std::span<const std::uint8_t> bytes) noexcept;

    [[nodiscard]] Result Mount(std::string_view path, std::span<const std::uint8_t> bytes);
    void Seal() noexcept { sealed_ = true; }
    [[nodiscard]] bool Sealed() const noexcept { return sealed_; }

    [[nodiscard]] Result Open(std::string_view path);
    [[nodiscard]] Result Close(std::uint32_t handle) noexcept;
    void CloseAll() noexcept { handles_.clear(); }
    [[nodiscard]] Result Peek(std::uint32_t handle, std::span<std::uint8_t> output) const noexcept;
    [[nodiscard]] Result PeekAt(std::uint32_t handle, std::uint64_t offset,
                                std::span<std::uint8_t> output) const noexcept;
    // Advance only after a guest-buffer write has succeeded.
    [[nodiscard]] Result Consume(std::uint32_t handle, std::size_t count) noexcept;
    [[nodiscard]] Result Seek(std::uint32_t handle, std::int64_t delta,
                              unsigned origin) noexcept;
    [[nodiscard]] Error Stat(std::string_view path, Info& info) const noexcept;
    [[nodiscard]] Error Fstat(std::uint32_t handle, Info& info) const noexcept;
    [[nodiscard]] bool Enumerate(std::size_t index, std::string_view& name,
                                 Info& info) const noexcept;

    [[nodiscard]] std::size_t AssetCount() const noexcept { return files_.size(); }
    [[nodiscard]] std::size_t OpenCount() const noexcept { return handles_.size(); }
    [[nodiscard]] std::size_t StoredBytes() const noexcept { return stored_bytes_; }

private:
    struct File {
        std::vector<std::uint8_t> bytes;
        std::uint64_t digest{};
    };
    using Files = std::map<std::string, File, std::less<>>;
    struct Descriptor {
        Files::const_iterator file;
        std::uint64_t offset{};
    };

    [[nodiscard]] static Result Copy(const File& file, std::uint64_t offset,
                                     std::span<std::uint8_t> output) noexcept;
    Files files_;
    std::unordered_map<std::uint32_t, Descriptor> handles_;
    std::size_t stored_bytes_{};
    std::uint32_t next_handle_{kFirstHandle};
    bool sealed_{};
};
} // namespace nint2::assets
