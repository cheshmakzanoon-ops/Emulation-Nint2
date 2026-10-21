#include "nint2/elf.hpp"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace nint2 {
namespace {
constexpr std::uint64_t kMaxImageBytes = 32ULL * 1024 * 1024;
constexpr std::uint64_t kMaxVirtualMapBytes = 128ULL * 1024 * 1024;
constexpr std::uint64_t kMaxRelocations = 65536;
constexpr unsigned kMaxProgramHeaders = 128;
constexpr std::uint64_t kPage = GuestMemory::kPageSize;
constexpr std::uint64_t kRelative = 1027; // ELF AArch64 R_AARCH64_RELATIVE

// Do not trust struct packing, the host endianness, or unaligned pointers.
[[nodiscard]] bool Read(std::span<const std::uint8_t> data,
                        std::uint64_t offset, unsigned bytes,
                        std::uint64_t& result) noexcept {
    if (offset > data.size() || bytes > data.size() - offset) return false;
    result = 0;
    for (unsigned i = 0; i < bytes; ++i)
        result |= std::uint64_t{data[static_cast<std::size_t>(offset+i)]} << (i*8);
    return true;
}
[[nodiscard]] bool Add(std::uint64_t a, std::uint64_t b,
                       std::uint64_t& out) noexcept {
    if (b > UINT64_MAX - a) return false;
    out = a + b;
    return true;
}
[[nodiscard]] bool RoundUp(std::uint64_t n, std::uint64_t& out) noexcept {
    if (n > UINT64_MAX - (kPage - 1)) return false;
    out = (n + kPage - 1) & ~(kPage - 1);
    return true;
}
[[nodiscard]] Access Permissions(std::uint64_t flags) noexcept {
    const auto r = (flags & 4) ? Access::Read : Access::None;
    const auto w = (flags & 2) ? Access::Write : Access::None;
    const auto x = (flags & 1) ? Access::Execute : Access::None;
    return r | w | x;
}
struct Segment {
    std::uint64_t offset{}, filesz{}, vaddr{}, memsz{}, base{}, length{};
    Access permissions{Access::None};
};
struct Dynamic {
    std::uint64_t offset{}, vaddr{}, size{};
};
struct Relocation {
    std::uint64_t address{}, value{};
};
// A single PT_LOAD must back the entire dynamic/rela file range.
[[nodiscard]] std::optional<std::uint64_t> FileOffset(
    const std::vector<Segment>& segments, std::uint64_t vaddr,
    std::uint64_t bytes) noexcept {
    for (const auto& s : segments) {
        if (vaddr < s.vaddr) continue;
        const auto delta = vaddr - s.vaddr;
        if (delta <= s.filesz && bytes <= s.filesz - delta)
            return s.offset + delta;
    }
    return std::nullopt;
}
[[nodiscard]] bool TargetWritable(const std::vector<Segment>& segments,
                                  std::uint64_t vaddr) noexcept {
    for (const auto& s : segments) {
        if (vaddr < s.vaddr) continue;
        const auto delta = vaddr - s.vaddr;
        if (delta <= s.memsz && s.memsz - delta >= 8)
            return HasAccess(s.permissions, Access::Write);
    }
    return false;
}
[[nodiscard]] bool RelocatedAddress(std::uint64_t bias,
                                    std::int64_t addend,
                                    std::uint64_t limit,
                                    std::uint64_t& output) noexcept {
    // Signed overflow and negating INT64_MIN are both forbidden.
    const auto raw = std::bit_cast<std::uint64_t>(addend);
    if (addend < 0) {
        const auto magnitude = std::uint64_t{0} - raw;
        if (magnitude > bias) return false;
        output = bias - magnitude;
    } else {
        if (!Add(bias, raw, output)) return false;
    }
    return output < limit;
}
[[nodiscard]] bool BenignDynamicTag(std::uint64_t tag) noexcept {
    // Non-binding metadata emitted by freestanding Clang/LLD static-PIEs.
    switch (tag) {
    case 4: case 5: case 6: case 10: case 11: case 21: case 30:
    case 0x6ffffef5ULL: case 0x6ffffff9ULL: case 0x6ffffffbULL:
        return true;
    default: return false;
    }
}
} // namespace

ElfResult LoadElf64(std::span<const std::uint8_t> image, GuestMemory& memory,
                    std::uint64_t pie_base) {
    const auto fail = [](ElfError error) { return ElfResult{error, 0, 0}; };
    if (image.size() < 64 || image.size() > kMaxImageBytes ||
        image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F' ||
        image[4] != 2 || image[5] != 1 || image[6] != 1)
        return fail(ElfError::InvalidHeader);
    std::uint64_t type{}, machine{}, version{}, entry{}, phoff{},
                  ehsize{}, phentsize{}, phnum{};
    if (!Read(image,16,2,type) || !Read(image,18,2,machine) ||
        !Read(image,20,4,version) || !Read(image,24,8,entry) ||
        !Read(image,32,8,phoff) || !Read(image,52,2,ehsize) ||
        !Read(image,54,2,phentsize) || !Read(image,56,2,phnum))
        return fail(ElfError::InvalidHeader);
    const bool pie = type == 3;
    if ((!pie && type != 2) || machine != 183 || version != 1 ||
        ehsize != 64 || phentsize != 56)
        return fail(ElfError::UnsupportedFormat);
    if (pie && pie_base % kPage != 0)
        return fail(ElfError::InvalidSegment);
    if (phnum == 0 || phnum > kMaxProgramHeaders || phoff > image.size() ||
        phnum * phentsize > image.size() - phoff)
        return fail(ElfError::InvalidHeader);

    // Parse ALL program headers before creating mappings.
    std::vector<Segment> segments;
    segments.reserve(static_cast<std::size_t>(phnum));
    std::optional<Dynamic> dynamic;
    std::uint64_t lowest_page = UINT64_MAX, total_mapped = 0;
    for (std::uint64_t i = 0; i < phnum; ++i) {
        const auto at = phoff + i * phentsize;
        std::uint64_t kind{}, flags{}, offset{}, vaddr{}, filesz{}, memsz{}, align{};
        if (!Read(image,at,4,kind) || !Read(image,at+4,4,flags) ||
            !Read(image,at+8,8,offset) || !Read(image,at+16,8,vaddr) ||
            !Read(image,at+32,8,filesz) || !Read(image,at+40,8,memsz) ||
            !Read(image,at+48,8,align))
            return fail(ElfError::InvalidHeader);
        if (kind == 3 || kind == 7) // PT_INTERP / PT_TLS
            return fail(ElfError::UnsupportedFormat);
        if (kind == 2) {
            if (!pie || dynamic || offset > image.size() ||
                filesz > image.size() - offset || filesz == 0 || filesz % 16)
                return fail(ElfError::UnsupportedFormat);
            dynamic = Dynamic{offset,vaddr,filesz};
            continue;
        }
        if (kind != 1) continue;
        if (flags > 7 || (flags & 3) == 3 || filesz > memsz ||
            offset > image.size() || filesz > image.size() - offset ||
            vaddr > UINT64_MAX - memsz ||
            (align > 1 && ((align & (align - 1)) != 0 ||
                           (vaddr % align) != (offset % align))))
            return fail(ElfError::InvalidSegment);
        if (memsz == 0) continue;
        const auto base = vaddr & ~(kPage - 1);
        std::uint64_t end{}, rounded{};
        if (!Add(vaddr,memsz,end) || !RoundUp(end,rounded))
            return fail(ElfError::InvalidSegment);
        const auto length = rounded - base;
        if (length == 0 || length > kMaxVirtualMapBytes - total_mapped)
            return fail(ElfError::ResourceLimit);
        total_mapped += length;
        for (const auto& old : segments)
            if (base < old.base + old.length && old.base < rounded)
                return fail(ElfError::InvalidSegment);
        segments.push_back({offset,filesz,vaddr,memsz,base,length,Permissions(flags)});
        lowest_page = std::min(lowest_page,base);
    }
    if (segments.empty()) return fail(ElfError::NoExecutableEntry);
    // All relocated addresses must fit guest memory without arithmetic wrap.
    if (pie && pie_base < lowest_page) return fail(ElfError::InvalidSegment);
    const std::uint64_t bias = pie ? pie_base - lowest_page : 0;
    for (const auto& seg : segments) {
        std::uint64_t end{};
        if (!Add(seg.base,bias,end) || !Add(end,seg.length,end) ||
            end > memory.AddressLimit())
            return fail(ElfError::MappingFailed);
    }
    std::uint64_t relocated_entry{};
    if (!Add(entry,bias,relocated_entry) || relocated_entry % 4)
        return fail(ElfError::NoExecutableEntry);
    bool executable_entry = false;
    for (const auto& seg : segments)
        if (HasAccess(seg.permissions,Access::Execute) &&
            entry >= seg.vaddr && seg.filesz >= 4 &&
            entry - seg.vaddr <= seg.filesz - 4)
            executable_entry = true;
    if (!executable_entry) return fail(ElfError::NoExecutableEntry);

    // Interpret only statically resolvable RELATIVE relocations. Reject all
    // unresolved symbols, dynamic libraries, PLT, TLS and unknown services.
    std::vector<Relocation> relocations;
    if (dynamic) {
        const auto table_offset = FileOffset(segments,dynamic->vaddr,dynamic->size);
        if (!table_offset || *table_offset != dynamic->offset)
            return fail(ElfError::InvalidRelocation);
        std::optional<std::uint64_t> rela_va,rela_size,rela_entry;
        bool terminated = false;
        for (std::uint64_t i = 0; i < dynamic->size; i += 16) {
            std::uint64_t tag{},value{};
            if (!Read(image,dynamic->offset+i,8,tag) ||
                !Read(image,dynamic->offset+i+8,8,value))
                return fail(ElfError::InvalidRelocation);
            if (tag == 0) { terminated = true; break; }
            if (tag == 7 || tag == 8 || tag == 9) {
                auto& field = tag == 7 ? rela_va : tag == 8 ? rela_size : rela_entry;
                if (field) return fail(ElfError::InvalidRelocation);
                field = value;
            } else if (!BenignDynamicTag(tag)) {
                return fail(ElfError::UnsupportedRelocation);
            }
        }
        if (!terminated) return fail(ElfError::InvalidRelocation);
        if (rela_va || rela_size || rela_entry) {
            if (!rela_va || !rela_size || !rela_entry || *rela_entry != 24 ||
                *rela_size % 24 || *rela_size / 24 > kMaxRelocations)
                return fail(ElfError::InvalidRelocation);
            if (*rela_size > 0) {
                const auto file = FileOffset(segments,*rela_va,*rela_size);
                if (!file) return fail(ElfError::InvalidRelocation);
                relocations.reserve(static_cast<std::size_t>(*rela_size / 24));
                for (std::uint64_t i = 0; i < *rela_size; i += 24) {
                    std::uint64_t site{},info{},raw_addend{};
                    if (!Read(image,*file+i,8,site) ||
                        !Read(image,*file+i+8,8,info) ||
                        !Read(image,*file+i+16,8,raw_addend))
                        return fail(ElfError::InvalidRelocation);
                    if ((info >> 32) != 0 || (info & 0xffffffffULL) != kRelative)
                        return fail(ElfError::UnsupportedRelocation);
                    if (site % 8 != 0 || !TargetWritable(segments,site))
                        return fail(ElfError::InvalidRelocation);
                    std::uint64_t target{},value{};
                    if (!Add(site,bias,target) ||
                        !RelocatedAddress(bias,std::bit_cast<std::int64_t>(raw_addend),
                                          memory.AddressLimit(),value))
                        return fail(ElfError::InvalidRelocation);
                    relocations.push_back({target,value});
                }
            }
        }
    }

    // Hostile relocation tables must not cause quadratic duplicate scans.
    // Reject duplicate writes before ANY guest mappings are changed.
    std::sort(relocations.begin(), relocations.end(),
              [](const Relocation& a, const Relocation& b) {
                  return a.address < b.address;
              });
    for (std::size_t i = 1; i < relocations.size(); ++i)
        if (relocations[i-1].address == relocations[i].address)
            return fail(ElfError::InvalidRelocation);

    // Two-phase commit: load temporary RW, apply checked relocations, then
    // protect RX/RW/R pages. Nothing runs while temporary permissions exist.
    std::size_t mapped = 0;
    const auto rollback = [&] {
        for (std::size_t i = 0; i < mapped; ++i)
            (void)memory.Unmap(segments[i].base+bias,segments[i].length);
    };
    for (const auto& seg : segments) {
        if (!memory.Map(seg.base+bias,seg.length,Access::Read|Access::Write)) {
            rollback();
            return fail(ElfError::MappingFailed);
        }
        ++mapped;
    }
    for (const auto& seg : segments) {
        if (seg.filesz &&
            !memory.WriteBuffer(seg.vaddr+bias,
                image.subspan(static_cast<std::size_t>(seg.offset),
                              static_cast<std::size_t>(seg.filesz)))) {
            rollback();
            return fail(ElfError::MappingFailed);
        }
    }
    for (const auto& relocation : relocations) {
        if (!memory.Write64(relocation.address,relocation.value)) {
            rollback();
            return fail(ElfError::MappingFailed);
        }
    }
    for (const auto& seg : segments) {
        if (!memory.Protect(seg.base+bias,seg.length,seg.permissions)) {
            rollback();
            return fail(ElfError::MappingFailed);
        }
    }
    return {ElfError::None,relocated_entry,bias};
}
const char* ElfErrorName(ElfError error) noexcept {
    switch(error) {
    case ElfError::None: return "none";
    case ElfError::InvalidHeader: return "invalid ELF header or truncated header table";
    case ElfError::UnsupportedFormat: return "unsupported ELF format or dynamic linking";
    case ElfError::InvalidSegment: return "invalid, overlapping or W+X program segment";
    case ElfError::ResourceLimit: return "executable exceeds guest mapping budget";
    case ElfError::NoExecutableEntry: return "entry not within executable file-backed segment";
    case ElfError::MappingFailed: return "guest memory mapping or segment copy failed";
    case ElfError::InvalidRelocation: return "invalid, overlapping or out-of-bounds PIE relocation";
    case ElfError::UnsupportedRelocation: return "unsupported dynamic service or relocation";
    }
    return "unknown";
}
} // namespace nint2
