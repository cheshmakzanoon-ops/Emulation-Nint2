#include "nint2/cpu.hpp"
#include <cstdint>
#include <optional>

namespace nint2 {

// Paired exclusives (ARMv8-A) and paired LSE CAS (ARMv8.1). The host
// interpreter has only one execution thread; no host atomics, raw pointers,
// native locks or claimed multicore/hardware coherence enter guest memory.
std::optional<CpuResult> CpuInterpreter::StepPair(GuestMemory& memory,
                                                  std::uint32_t op) {
    if ((op & 0x3FA00000U) != 0x08200000U)
        return std::nullopt;

    const auto pc = context.pc;
    const auto done = [&]() -> CpuResult {
        context.pc = pc + 4;
        return {CpuStop::Running, 1, 0, op};
    };
    const auto illegal = [&]() -> CpuResult {
        return {CpuStop::UndefinedInstruction, 1, pc, op};
    };
    const auto fault = [&](std::uint64_t address) -> CpuResult {
        return {CpuStop::DataFault, 1, address, op};
    };
    const unsigned size_bits = (op >> 30) & 3U;
    const unsigned rn = (op >> 5) & 31U;
    const unsigned rt = op & 31U;
    const unsigned rs = (op >> 16) & 31U;
    const auto address = ReadReg(rn, true, true);

    // CASP{A,L,AL}: size 0 means 2x W, size 1 means 2x X. The
    // expected and desired register PAIRS must begin at even numbers.
    // Bits 14:10 are fixed to 11111, unlike exclusive LDXP/STXP.
    if (size_bits <= 1) {
        if ((op & 0x00007C00U) != 0x00007C00U ||
            (rs & 1U) != 0 || (rt & 1U) != 0)
            return illegal();
        const unsigned width = size_bits == 0 ? 4U : 8U;
        if ((rn == 31U && (address & 15U)) ||
            address % (2 * width) != 0)
            return fault(address);

        // Read all operands BEFORE writing the old pair back to expected
        // registers: CASP can overwrite both expected registers on mismatch.
        const auto expected_first = ReadReg(rs, width == 8);
        const auto expected_second = ReadReg(rs + 1, width == 8);
        const auto desired_first = ReadReg(rt, width == 8);
        const auto desired_second = ReadReg(rt + 1, width == 8);
        const auto old = memory.AtomicComparePair(
            address, width, expected_first, expected_second,
            desired_first, desired_second);
        if (!old) return fault(address);
        WriteReg(rs, old->first, width == 8);
        WriteReg(rs + 1, old->second, width == 8);
        return done();
    }

    // LDXP/LDAXP and STXP/STLXP: total size 8 (W pair) or 16 (X
    // pair). A separate Rt2 field occupies bits 14:10; never mask it as
    // an opcode bit or upper numbered registers would falsely trap.
    const unsigned width = size_bits == 2 ? 4U : 8U;
    const unsigned rt2 = (op >> 10) & 31U;
    const bool load = (op & (1U << 22)) != 0;
    if (rt == rt2 || (!load && (rs == rt || rs == rt2)) ||
        (load && rs != 31U))
        return illegal();
    if ((rn == 31U && (address & 15U)) ||
        address % (2 * width) != 0)
        return fault(address);

    if (load) {
        const auto pair = memory.LoadExclusivePair(address, width);
        if (!pair) return fault(address);
        // Faults leave both destinations and the monitor unchanged.
        WriteReg(rt, pair->first, width == 8);
        WriteReg(rt2, pair->second, width == 8);
        exclusive_.valid = true;
        exclusive_.space = &memory;
        exclusive_.address = address;
        exclusive_.bytes = 2 * width;
        exclusive_.frame_id = pair->frame_id;
        exclusive_.generation = pair->generation;
        return done();
    }

    const bool matches = exclusive_.valid && exclusive_.space == &memory &&
                         exclusive_.address == address &&
                         exclusive_.bytes == 2 * width;
    const auto frame_id = matches ? exclusive_.frame_id : 0;
    const auto generation = matches ? exclusive_.generation : 0;
    const auto first = ReadReg(rt, width == 8);
    const auto second = ReadReg(rt2, width == 8);
    ClearExclusive(); // STXP consumes the reservation on both outcomes.
    const auto status = memory.StoreExclusivePair(
        address, width, first, second, frame_id, generation);
    if (status == GuestMemory::ExclusiveStore::Fault)
        return fault(address);
    WriteReg(rs, status == GuestMemory::ExclusiveStore::Stored ? 0 : 1, false);
    return done();
}
} // namespace nint2
