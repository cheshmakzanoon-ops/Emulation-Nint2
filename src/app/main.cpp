#include <iostream>
#include <string_view>
#include "nint2/cpu.hpp"
#include "nint2/memory.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: nint2 --help | --version | --smoke | --cpu-smoke\n";
        return 2;
    }
    const std::string_view option = argv[1];
    if (option == "--help") {
        std::cout << "nint2 research runtime: --help | --version | --smoke | --cpu-smoke\n"
                     "Original AArch64 tests only; no Switch 2 game/firmware support.\n";
        return 0;
    }
    if (option == "--version") {
        std::cout << "nint2 0.1.0 (experimental interpreter)\n";
        return 0;
    }
    if (option == "--smoke") {
        nint2::GuestMemory memory(0x10000);
        if (!memory.Map(0x1000, nint2::GuestMemory::kPageSize,
                        nint2::Access::Read | nint2::Access::Write) ||
            !memory.Write8(0x1000, 42) || memory.Read8(0x1000) != 42) {
            std::cerr << "Host guest-memory smoke failed\n";
            return 1;
        }
        std::cout << "Host guest-memory smoke passed: 42\n";
        return 0;
    }
    if (option == "--cpu-smoke") {
        nint2::GuestMemory memory(0x10000);
        constexpr std::uint64_t base = 0x1000;
        if (!memory.Map(base, nint2::GuestMemory::kPageSize,
                        nint2::Access::Read | nint2::Access::Write) ||
            !memory.Write32(base, 0x528000e0) ||     // movz w0,#7
            !memory.Write32(base + 4, 0x11008c00) || // add w0,w0,#35
            !memory.Write32(base + 8, 0xd4200000) || // brk #0
            !memory.Protect(base, nint2::GuestMemory::kPageSize,
                            nint2::Access::Read | nint2::Access::Execute)) return 1;
        nint2::CpuInterpreter cpu;
        cpu.context.pc = base;
        const auto result = cpu.Run(memory, 32);
        if (result.stop != nint2::CpuStop::Breakpoint || cpu.context.x[0] != 42) {
            std::cerr << "AArch64 original machine-code fixture failed\n";
            return 1;
        }
        std::cout << "AArch64 interpreter smoke passed: x0=" << cpu.context.x[0]
                  << " steps=" << result.steps << '\n';
        return 0;
    }
    std::cerr << "Unknown argument: " << option << "\n";
    return 2;
}
