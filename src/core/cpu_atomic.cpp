#include "nint2/cpu.hpp"
#include <cstdint>
#include <optional>

namespace nint2 {
std::optional<CpuResult> CpuInterpreter::StepAtomic(GuestMemory& memory,
                                                     std::uint32_t op) {
    const auto pc = context.pc;
    const auto done = [&]() -> CpuResult {
        context.pc = pc + 4;
        return {CpuStop::Running, 1, 0, op};
    };
    const auto illegal = [&]() -> CpuResult {
        return {CpuStop::UndefinedInstruction, 1, pc, op};
    };

    // CLREX <imm4> clears only this execution context's monitor.
    // This is safe across scheduler quanta and guest processes.
    if ((op & 0xfffff0ffU) == 0xd503305fU) {
        ClearExclusive();
        return done();
    }
    // Barriers are synchronization points in the deterministic, single-host-
    // thread interpreter. They do not fake host cache coherence or GPU fences.
    // Reject reserved barrier options rather than silently treating all as NOP.
    const auto barrier = op & 0xfffff0ffU;
    if (barrier == 0xd503309fU || barrier == 0xd50330bfU ||
        barrier == 0xd50330dfU) {
        const unsigned option = (op >> 8) & 15;
        if (barrier == 0xd50330dfU) {
            if (option != 15) return illegal(); // ISB SY only
        } else {
            // DMB/DSB domains/types defined by AArch64, not option 0.
            if (option == 0 || option == 4 || option == 8 || option == 12)
                return illegal();
        }
        return done();
    }
    // LSE is architecturally disjoint from the older exclusive encodings,
    // but shares their 0x08 top-level opcode group.
    if (const auto lse = StepLse(memory, op)) return *lse;
    // LDXR/LDAXR/STXR/STLXR byte, halfword, W and X forms. Reject pair,
    // acquire/release single-register and LSE opcode families for now.
    if ((op & 0x3f000000U) != 0x08000000U)
        return std::nullopt;
    if ((op & (1U << 23)) != 0 || (op & (1U << 21)) != 0 ||
        ((op >> 10) & 31U) != 31U)
        return illegal();
    const bool load = (op & (1U << 22)) != 0;
    const unsigned bytes = 1U << ((op >> 30) & 3U);
    const unsigned rt = op & 31U;
    const unsigned rn = (op >> 5) & 31U;
    const unsigned rs = (op >> 16) & 31U;
    if (load && rs != 31U) return illegal();
    const auto address = ReadReg(rn, true, true);
    // ARM's stack register is required to be 16-byte aligned when used as
    // an address base, regardless of access size.
    if ((rn == 31U && (address & 15U)) || (address % bytes != 0))
        return CpuResult{CpuStop::DataFault, 1, address, op};

    if (load) {
        const auto read = memory.LoadExclusive(address, bytes);
        if (!read) return CpuResult{CpuStop::DataFault, 1, address, op};
        WriteReg(rt, read->value, bytes == 8);
        exclusive_.valid = true;
        exclusive_.space = &memory;
        exclusive_.address = address;
        exclusive_.bytes = bytes;
        exclusive_.frame_id = read->frame_id;
        exclusive_.generation = read->generation;
        return done();
    }

    // STXR consumes the monitor, even after an address mismatch or failed
    // attempt. A lost reservation never modifies memory or the data operand.
    const bool matches = exclusive_.valid && exclusive_.space == &memory &&
                         exclusive_.address == address &&
                         exclusive_.bytes == bytes;
    const auto id = matches ? exclusive_.frame_id : 0;
    const auto generation = matches ? exclusive_.generation : 0;
    const auto value = ReadReg(rt, bytes == 8);
    ClearExclusive();
    const auto result = memory.StoreExclusive(address, bytes, value,
                                               id, generation);
    if (result == GuestMemory::ExclusiveStore::Fault)
        return CpuResult{CpuStop::DataFault, 1, address, op};
    WriteReg(rs, result == GuestMemory::ExclusiveStore::Stored ? 0 : 1, false);
    return done();
}
} // namespace nint2
