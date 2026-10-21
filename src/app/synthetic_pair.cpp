#include "synthetic_pair.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/scheduler.hpp"

namespace {
constexpr std::uint64_t kAddressSpace = 0x40000000;
constexpr std::uint64_t kStackBase = 0x3fff0000;
constexpr std::uint64_t kStackSize = 0x10000;
constexpr std::uint64_t kReportBase = 0x3ffe0000;

bool LoadOriginal(const char* path, nint2::GuestMemory& memory,
                  std::uint64_t& entry) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 32 * 1024 * 1024) {
        std::cerr << "Cannot read ELF (missing/empty/over 32 MiB): " << path << '\n';
        return false;
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(image.data()),
                   static_cast<std::streamsize>(image.size()))) {
        std::cerr << "Failed to read complete ELF file: " << path << '\n';
        return false;
    }
    const auto loaded = nint2::LoadElf64(image, memory);
    if (loaded.error != nint2::ElfError::None) {
        std::cerr << "ELF rejected: " << nint2::ElfErrorName(loaded.error)
                  << ": " << path << '\n';
        return false;
    }
    if (!memory.Map(kStackBase, kStackSize, nint2::Access::Read | nint2::Access::Write) ||
        !memory.Map(kReportBase, nint2::GuestMemory::kPageSize,
                    nint2::Access::Read | nint2::Access::Write)) {
        std::cerr << "Cannot map private guest stack/report: " << path << '\n';
        return false;
    }
    entry = loaded.entry;
    return true;
}

void PrintResult(const char* name, nint2::SyntheticSession& session) {
    const auto outcome = session.Run(0);
    // Run(0) reports whether Exit is sticky, not the historical fault reason.
    std::cerr << name << ": state="
              << (outcome.stop == nint2::SessionStop::Exited ? "exited" : "not-exited")
              << " exit=" << outcome.exit_code << " steps=" << outcome.steps
              << " pc=0x" << std::hex << session.Context().pc << std::dec << '\n';
}
} // namespace

int RunSyntheticPair(const char* client_path, const char* server_path) {
    // Two identical virtual layouts with completely separate memory objects.
    nint2::GuestMemory client_memory(kAddressSpace), server_memory(kAddressSpace);
    std::uint64_t client_entry{}, server_entry{};
    if (!LoadOriginal(client_path, client_memory, client_entry) ||
        !LoadOriginal(server_path, server_memory, server_entry)) return 2;
    nint2::SyntheticSession client(client_memory, client_entry, kStackBase + kStackSize);
    nint2::SyntheticSession server(server_memory, server_entry, kStackBase + kStackSize);

    nint2::kernel::HandleTable bootstrap;
    const auto request = bootstrap.CreateQueue(1);
    const auto response = bootstrap.CreateQueue(1);
    if (!request.Ok() || !response.Ok()) {
        std::cerr << "Cannot allocate synthetic request/reply queues\n";
        return 1;
    }
    const auto requests = static_cast<std::uint32_t>(request.value);
    const auto responses = static_cast<std::uint32_t>(response.value);
    using nint2::kernel::Right;
    const auto client_send = client.ImportQueue(bootstrap, requests, Right::Send);
    const auto client_receive = client.ImportQueue(bootstrap, responses, Right::Receive);
    const auto server_send = server.ImportQueue(bootstrap, responses, Right::Send);
    const auto server_receive = server.ImportQueue(bootstrap, requests, Right::Receive);
    if (!client_send.Ok() || !client_receive.Ok() ||
        !server_send.Ok() || !server_receive.Ok()) {
        std::cerr << "Cannot provision restricted synthetic queue capabilities\n";
        return 1;
    }
    if (!bootstrap.Close(requests).Ok() || !bootstrap.Close(responses).Ok()) return 1;
    client.Context().x[0] = client_send.value;
    client.Context().x[1] = client_receive.value;
    client.Context().x[2] = kReportBase;
    server.Context().x[0] = server_send.value;
    server.Context().x[1] = server_receive.value;
    server.Context().x[2] = kReportBase;

    nint2::SyntheticScheduler scheduler;
    if (!scheduler.Add(client) || !scheduler.Add(server)) return 1;
    const auto result = scheduler.Run(1000000, 64);
    std::cout << client.Stdout() << server.Stdout();
    std::cerr << client.Stderr() << server.Stderr();
    std::cerr << "Synthetic pair: stop=" << static_cast<unsigned>(result.stop)
              << " exited=" << result.exited << " failed=" << result.failed
              << " steps=" << result.executed_steps << '\n';
    PrintResult("Client", client);
    PrintResult("Server", server);
    return result.stop == nint2::SchedulerStop::Completed && result.exited == 2 &&
                   result.failed == 0 && client.Run(0).exit_code == 0 &&
                   server.Run(0).exit_code == 0
               ? 0 : 1;
}
