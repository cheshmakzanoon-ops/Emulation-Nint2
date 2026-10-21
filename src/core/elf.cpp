#include "nint2/elf.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace nint2 {
namespace {
constexpr std::uint64_t kMaxImageBytes = 32ULL * 1024 * 1024;
constexpr std::uint64_t kMaxVirtualMapBytes = 128ULL * 1024 * 1024;
constexpr unsigned kMaxProgramHeaders = 128;
constexpr std::uint64_t kPage = GuestMemory::kPageSize;

[[nodiscard]] bool Read(std::span<const std::uint8_t> data,
                        std::uint64_t offset, unsigned bytes, std::uint64_t& result) {
    if (offset > data.size() || bytes > data.size() - offset) return false;
    result = 0;
    for (unsigned i = 0; i < bytes; ++i) result |= std::uint64_t{data[offset+i]} << (i*8);
    return true;
}
struct Segment {
    std::uint64_t offset{}, file_size{}, base{}, length{}, vaddr{};
    Access permissions{Access::None};
};
[[nodiscard]] Access Permissions(std::uint64_t flags) {
    const auto read = (flags & 4) ? Access::Read : Access::None;
    const auto write = (flags & 2) ? Access::Write : Access::None;
    const auto exec = (flags & 1) ? Access::Execute : Access::None;
    return read | write | exec;
}
} // namespace

ElfResult LoadElf64(std::span<const std::uint8_t> image, GuestMemory& memory) {
    if (image.size() < 64 || image.size() > kMaxImageBytes ||
        image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F' ||
        image[4] != 2 || image[5] != 1 || image[6] != 1)
        return {ElfError::InvalidHeader, 0};
    std::uint64_t type{}, machine{}, version{}, entry{}, phoff{}, ehsize{}, phentsize{}, phnum{};
    if (!Read(image,16,2,type) || !Read(image,18,2,machine) || !Read(image,20,4,version) ||
        !Read(image,24,8,entry) || !Read(image,32,8,phoff) || !Read(image,52,2,ehsize) ||
        !Read(image,54,2,phentsize) || !Read(image,56,2,phnum))
        return {ElfError::InvalidHeader, 0};
    if (type != 2 || machine != 183 || version != 1 || ehsize != 64 || phentsize != 56)
        return {ElfError::UnsupportedFormat, 0};
    if (phnum == 0 || phnum > kMaxProgramHeaders || phoff > image.size() ||
        phnum * phentsize > image.size() - phoff)
        return {ElfError::InvalidHeader, 0};

    std::vector<Segment> segments;
    segments.reserve(static_cast<std::size_t>(phnum));
    std::uint64_t total_mapped = 0;
    bool executable_entry = false;
    for (std::uint64_t i = 0; i < phnum; ++i) {
        const auto start = phoff + i * phentsize;
        std::uint64_t kind{}, flags{}, offset{}, vaddr{}, filesz{}, memsz{}, align{};
        if (!Read(image,start,4,kind) || !Read(image,start+4,4,flags) ||
            !Read(image,start+8,8,offset) || !Read(image,start+16,8,vaddr) ||
            !Read(image,start+32,8,filesz) || !Read(image,start+40,8,memsz) ||
            !Read(image,start+48,8,align)) return {ElfError::InvalidHeader, 0};
        if (kind == 2 || kind == 3) return {ElfError::UnsupportedFormat, 0};
        if (kind != 1) continue;
        if (flags > 7 || ((flags & 3) == 3) || filesz > memsz ||
            offset > image.size() || filesz > image.size() - offset ||
            vaddr > UINT64_MAX - memsz ||
            (align > 1 && ((align & (align - 1)) != 0 || (vaddr % align) != (offset % align))))
            return {ElfError::InvalidSegment, 0};
        if (memsz == 0) continue;
        const auto end = vaddr + memsz;
        if (end > UINT64_MAX - (kPage - 1)) return {ElfError::InvalidSegment, 0};
        const auto base = vaddr & ~(kPage - 1);
        const auto length = ((end + (kPage - 1)) & ~(kPage - 1)) - base;
        if (length == 0 || length > kMaxVirtualMapBytes - total_mapped)
            return {ElfError::ResourceLimit, 0};
        total_mapped += length;
        for (const auto& old : segments) {
            if (base < old.base + old.length && old.base < base + length)
                return {ElfError::InvalidSegment, 0};
        }
        if ((flags & 1) && (entry % 4 == 0) && entry >= vaddr &&
            filesz >= 4 && entry - vaddr <= filesz - 4) executable_entry = true;
        segments.push_back({offset, filesz, base, length, vaddr, Permissions(flags)});
    }
    if (!executable_entry) return {ElfError::NoExecutableEntry, 0};

    std::size_t mapped = 0;
    auto rollback = [&]() {
        for (std::size_t i = 0; i < mapped; ++i)
            (void)memory.Unmap(segments[i].base, segments[i].length);
    };
    for (const auto& seg : segments) {
        if (!memory.Map(seg.base, seg.length, Access::Read | Access::Write)) {
            rollback();
            return {ElfError::MappingFailed, 0};
        }
        ++mapped;
    }
    for (const auto& seg : segments) {
        for (std::uint64_t i = 0; i < seg.file_size; ++i) {
            if (!memory.Write8(seg.vaddr + i, image[seg.offset + i])) {
                rollback();
                return {ElfError::MappingFailed, 0};
            }
        }
    }
    for (const auto& seg : segments) {
        if (!memory.Protect(seg.base, seg.length, seg.permissions)) {
            rollback();
            return {ElfError::MappingFailed, 0};
        }
    }
    return {ElfError::None, entry};
}

const char* ElfErrorName(ElfError error) noexcept {
    switch (error) {
    case ElfError::None: return "none";
    case ElfError::InvalidHeader: return "invalid ELF header or truncated header table";
    case ElfError::UnsupportedFormat: return "unsupported ELF format or dynamic linking";
    case ElfError::InvalidSegment: return "invalid, overlapping or W+X program segment";
    case ElfError::ResourceLimit: return "executable exceeds guest mapping budget";
    case ElfError::NoExecutableEntry: return "entry not within executable file-backed segment";
    case ElfError::MappingFailed: return "guest memory mapping or segment copy failed";
    }
    return "unknown";
}
} // namespace nint2
