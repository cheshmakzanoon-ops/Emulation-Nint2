#include "nint2/cpu.hpp"
#include <cstdint>
#include <optional>

namespace nint2 {
namespace {
// AArch64 CRC32/CRC32C use reflected polynomials and feed the least
// significant source byte first. No initial/final XOR is performed by CRC32*.
[[nodiscard]] std::uint32_t UpdateCrc(std::uint32_t acc,
                                      std::uint64_t data,
                                      unsigned bytes,
                                      std::uint32_t polynomial) noexcept {
    for (unsigned byte = 0; byte < bytes; ++byte) {
        acc ^= static_cast<std::uint8_t>(data);
        data >>= 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            acc = (acc >> 1) ^ ((acc & 1U) ? polynomial : 0U);
    }
    return acc;
}
} // namespace

// Architectural A64 CRC32B/H/W/X and CRC32CB/CH/CW/CX. This is pure
// integer software, not a host CPU CRC instruction or a console ABI.
std::optional<CpuResult> CpuInterpreter::StepCrc(std::uint32_t op) {
    if ((op & 0x7FE0E000U) != 0x1AC04000U)
        return std::nullopt;
    const auto pc = context.pc;
    const unsigned opcode = (op >> 10) & 63U;
    if (opcode < 16 || opcode > 23)
        return CpuResult{CpuStop::UndefinedInstruction, 1, pc, op};
    const bool wide_source = (op >> 31) != 0;
    const unsigned width_code = opcode & 3U;
    // A64 reserves sf=1 for B/H/W and sf=0 for X. Do not treat illegal
    // encodings as another valid CRC width.
    if (wide_source != (width_code == 3))
        return CpuResult{CpuStop::UndefinedInstruction, 1, pc, op};
    const unsigned bytes = 1U << width_code;
    const unsigned rd = op & 31U;
    const unsigned rn = (op >> 5) & 31U;
    const unsigned rm = (op >> 16) & 31U;
    const std::uint32_t acc = static_cast<std::uint32_t>(ReadReg(rn, false));
    const std::uint64_t data = ReadReg(rm, wide_source);
    constexpr std::uint32_t kIeeeReversed = 0xEDB88320U;
    constexpr std::uint32_t kCastagnoliReversed = 0x82F63B78U;
    const std::uint32_t polynomial = (opcode & 4U) != 0 ?
                                     kCastagnoliReversed : kIeeeReversed;
    const auto value = UpdateCrc(acc, data, bytes, polynomial);
    WriteReg(rd, value, false);
    context.pc = pc + 4;
    return CpuResult{CpuStop::Running, 1, 0, op};
}
} // namespace nint2
