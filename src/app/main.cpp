#include <iostream>
#include <string_view>
#include "nint2/memory.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: nint2 --help | --version | --smoke\n";
        return 2;
    }
    const std::string_view option = argv[1];
    if (option == "--help") {
        std::cout << "nint2 research bootstrap: --help | --version | --smoke\n"
                     "No ARM64 guest execution or Switch 2 game support yet.\n";
        return 0;
    }
    if (option == "--version") {
        std::cout << "nint2 0.1.0 (M0 bootstrap)\n";
        return 0;
    }
    if (option == "--smoke") {
        nint2::GuestMemory memory(0x10000);
        if (!memory.Map(0x1000, nint2::GuestMemory::kPageSize,
                        nint2::Access::Read | nint2::Access::Write) ||
            !memory.Write8(0x1000, 42) || memory.Read8(0x1000) != 42) {
            std::cerr << "M0 native guest-memory smoke failed\n";
            return 1;
        }
        std::cout << "M0 native guest-memory smoke passed: 42\n";
        return 0;
    }
    std::cerr << "Unknown argument: " << option << "\n";
    return 2;
}
