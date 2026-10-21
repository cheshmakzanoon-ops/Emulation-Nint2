#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/scheduler.hpp"
#include "fixtures/compiled_rpc_images.hpp"

namespace {
using nint2::Access;
using nint2::GuestMemory;
using nint2::SchedulerStop;
using nint2::SessionStop;
using nint2::SyntheticSession;
using nint2::kernel::Right;
constexpr std::uint64_t kReport = 0x3ffe0000;
constexpr std::uint64_t kStack = 0x3fff0000;
constexpr std::uint64_t kBudget = 200000;
constexpr std::string_view kClientOutput = "C RPC client: 64 replies verified!\n";
constexpr std::string_view kServerOutput = "C RPC server: 64 requests processed!\n";

void Require(bool okay, const char* reason) {
    if (!okay) throw std::runtime_error(reason);
}

void Put(std::vector<std::uint8_t>& bytes, std::size_t at,
         std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> MakeElf(const original_rpc::Image& image) {
    // Repackage exact independently linked PT_LOAD bytes into a small original
    // static ELF. Preserve every segment's address, permissions and BSS size.
    std::vector<std::uint8_t> bytes(64 + image.segments.size() * 56);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    Put(bytes, 16, 2, 2); Put(bytes, 18, 183, 2); Put(bytes, 20, 1, 4);
    Put(bytes, 24, image.entry, 8); Put(bytes, 32, 64, 8);
    Put(bytes, 52, 64, 2); Put(bytes, 54, 56, 2);
    Put(bytes, 56, image.segments.size(), 2);
    for (std::size_t i = 0; i < image.segments.size(); ++i) {
        const auto& segment = image.segments[i];
        const auto offset = ((bytes.size() + 4095) & ~std::size_t{4095}) +
                            static_cast<std::size_t>(segment.address & 4095);
        bytes.resize(offset + segment.file_size);
        const auto header = 64 + 56 * i;
        Put(bytes, header, 1, 4); Put(bytes, header + 4, segment.flags, 4);
        Put(bytes, header + 8, offset, 8); Put(bytes, header + 16, segment.address, 8);
        Put(bytes, header + 32, segment.file_size, 8);
        Put(bytes, header + 40, segment.memory_size, 8);
        Put(bytes, header + 48, 4096, 8);
        for (std::size_t j = 0; j < segment.file_size; ++j)
            bytes[offset + j] = static_cast<std::uint8_t>(
                segment.words[j / 4] >> (8 * (j % 4)));
    }
    return bytes;
}

std::vector<std::uint8_t> ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(file && file.tellg() > 0 && file.tellg() <= 32 * 1024 * 1024,
            "missing, empty or oversized original ELF");
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint8_t> bytes(size);
    file.seekg(0);
    Require(static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()),
                                       static_cast<std::streamsize>(bytes.size()))),
            "cannot read complete original ELF");
    return bytes;
}
void WriteFile(const char* path, std::span<const std::uint8_t> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(file.write(reinterpret_cast<const char*>(bytes.data()),
                                        static_cast<std::streamsize>(bytes.size()))),
            "cannot write original ELF");
}

std::uint64_t ExpectedDigest() {
    // Host oracle is independent of both guest execution and guest report data.
    std::uint64_t digest = 0;
    for (std::uint64_t sequence = 1; sequence <= 64; ++sequence) {
        const auto request = (sequence * 0x100000001ULL) ^ 0x3141592653589793ULL;
        const auto reply = std::rotr(request ^ 0x1020304050607080ULL, 17) + sequence * 33;
        digest = std::rotr(digest ^ reply, 7) + sequence;
    }
    return digest;
}

struct Observation {
    nint2::SchedulerResult scheduler;
    nint2::SessionResult client, server;
    std::array<std::uint64_t, 8> client_report{}, server_report{};
    std::string client_stdout, server_stdout;
    std::size_t client_handles{}, server_handles{};
};

Observation Execute(std::span<const std::uint8_t> client_elf,
                    std::span<const std::uint8_t> server_elf,
                    std::uint64_t quantum, std::uint64_t corrupt_sequence = 0,
                    std::uint64_t final_exit_code = 0, bool reports = true) {
    GuestMemory client_memory(0x40000000), server_memory(0x40000000);
    const auto client_image = nint2::LoadElf64(client_elf, client_memory);
    const auto server_image = nint2::LoadElf64(server_elf, server_memory);
    Require(client_image.error == nint2::ElfError::None &&
            server_image.error == nint2::ElfError::None, "RPC ELF loader rejected a guest");
    for (auto* memory : {&client_memory, &server_memory}) {
        Require(memory->Map(kStack, 0x10000, Access::Read | Access::Write), "private stack map");
        Require(memory->Map(kReport, 4096, Access::Read | Access::Write), "private report map");
    }
    Require(client_memory.Write64(kReport + 48, corrupt_sequence) &&
            client_memory.Write64(kReport + 56, final_exit_code), "fixture fault controls");
    Require(client_memory.Write64(kReport + 128, 0x12345678) &&
            server_memory.Write64(kReport + 128, 0x87654321), "private sentinel setup");

    SyntheticSession client(client_memory, client_image.entry, kStack + 0x10000);
    SyntheticSession server(server_memory, server_image.entry, kStack + 0x10000);
    nint2::kernel::HandleTable bootstrap;
    const auto requests = bootstrap.CreateQueue(1);
    const auto replies = bootstrap.CreateQueue(1);
    Require(requests.Ok() && replies.Ok(), "host bootstrap queue creation");
    const auto request_send = client.ImportQueue(bootstrap, static_cast<std::uint32_t>(requests.value), Right::Send);
    const auto reply_receive = client.ImportQueue(bootstrap, static_cast<std::uint32_t>(replies.value), Right::Receive);
    const auto reply_send = server.ImportQueue(bootstrap, static_cast<std::uint32_t>(replies.value), Right::Send);
    const auto request_receive = server.ImportQueue(bootstrap, static_cast<std::uint32_t>(requests.value), Right::Receive);
    Require(request_send.Ok() && reply_receive.Ok() && reply_send.Ok() && request_receive.Ok(),
            "direction-scoped queue imports");
    Require(bootstrap.Close(static_cast<std::uint32_t>(requests.value)).Ok() &&
            bootstrap.Close(static_cast<std::uint32_t>(replies.value)).Ok() &&
            bootstrap.Size() == 0, "bootstrap owner releases both queue references");
    client.Context().x[0] = request_send.value;
    client.Context().x[1] = reply_receive.value;
    server.Context().x[0] = reply_send.value;
    server.Context().x[1] = request_receive.value;
    client.Context().x[2] = server.Context().x[2] = reports ? kReport : 0;

    // Run the server alone until it has attempted an empty receive. The bound
    // is a budget, not a compiler-specific instruction count for reaching SVC.
    std::uint64_t primed_steps = 0;
    if (reports) {
        while (server_memory.Read64(kReport + 24).value_or(0) == 0 && primed_steps < 4096) {
            const auto primed = server.Run(1);
            Require(primed.stop == SessionStop::StepLimit, "server failed while priming empty queue");
            ++primed_steps;
        }
        Require(server_memory.Read64(kReport + 24).value_or(0) > 0,
                "server did not exercise empty-queue retry");
        Require(client_memory.Read64(kReport + 24) == 0,
                "server modified the client's separate report mapping");
    }

    nint2::SyntheticScheduler scheduler;
    Require(scheduler.Add(client) && scheduler.Add(server), "RPC scheduler registration");
    auto result = scheduler.Run(0, quantum);
    Require(result.stop == SchedulerStop::BudgetExhausted && result.executed_steps == 0,
            "zero-budget scheduling changed guest state");
    // Small aggregate budgets force repeated suspension/resumption mid-function,
    // mid-transaction, and between SVC retry attempts.
    do {
        const auto remaining = kBudget - result.executed_steps;
        const auto before = result.executed_steps;
        result = scheduler.Run(std::min<std::uint64_t>(997, remaining), quantum);
        Require(result.executed_steps - before <= 997, "aggregate scheduling budget exceeded");
    } while (result.stop == SchedulerStop::BudgetExhausted && result.executed_steps < kBudget);

    Observation observed;
    observed.scheduler = result;
    observed.client = client.Run(0);
    observed.server = server.Run(0);
    observed.client_stdout = client.Stdout();
    observed.server_stdout = server.Stdout();
    observed.client_handles = client.HandleCount();
    observed.server_handles = server.HandleCount();
    Require(result.executed_steps + primed_steps == client.TotalSteps() + server.TotalSteps(),
            "scheduler lost or double-counted guest instructions");
    for (std::size_t i = 0; i < 8; ++i) {
        observed.client_report[i] = client_memory.Read64(kReport + 8 * i).value();
        observed.server_report[i] = server_memory.Read64(kReport + 8 * i).value();
    }
    Require(client_memory.Read64(kReport + 128) == 0x12345678 &&
            server_memory.Read64(kReport + 128) == 0x87654321,
            "same-address private guest sentinels were modified");
    if (result.failed != 0) {
        std::cerr << "RPC CPU stop: client pc=0x" << std::hex << client.Context().pc
                  << " instruction=0x" << client_memory.Fetch32(client.Context().pc).value_or(0)
                  << " server pc=0x" << server.Context().pc
                  << " instruction=0x" << server_memory.Fetch32(server.Context().pc).value_or(0)
                  << std::dec << '\n';
    }
    return observed;
}

bool Successful(const Observation& value) {
    return value.scheduler.stop == SchedulerStop::Completed &&
           value.scheduler.exited == 2 && value.scheduler.failed == 0 &&
           value.client.stop == SessionStop::Exited && value.server.stop == SessionStop::Exited &&
           value.client.exit_code == 0 && value.server.exit_code == 0 &&
           value.client_stdout == kClientOutput && value.server_stdout == kServerOutput &&
           value.client_handles == 0 && value.server_handles == 0;
}

void CheckSuccessful(const Observation& value, bool reports = true) {
    if (!Successful(value)) {
        std::cerr << "RPC status=" << static_cast<unsigned>(value.scheduler.stop)
                  << " client exit=" << value.client.exit_code
                  << " server exit=" << value.server.exit_code
                  << " steps=" << value.scheduler.executed_steps << '\n';
        throw std::runtime_error("compiled C RPC did not complete successfully");
    }
    if (!reports) return;
    for (const auto* report : {&value.client_report, &value.server_report}) {
        Require((*report)[0] == 64, "not all guest requests/replies completed");
        Require((*report)[1] == ExpectedDigest(), "guest checksum differs from host oracle");
        Require((*report)[4] == 2, "guest capability-denial checks did not execute");
        Require((*report)[5] == 0, "guest report exit status is nonzero");
    }
    Require(value.server_report[3] > 0, "empty receive retry was not observed");
}

void RunTests(std::span<const std::uint8_t> client, std::span<const std::uint8_t> server) {
    std::uint64_t send_retries = 0;
    for (const std::uint64_t quantum : {1, 7, 64, 257}) {
        const auto first = Execute(client, server, quantum);
        const auto replay = Execute(client, server, quantum);
        CheckSuccessful(first);
        CheckSuccessful(replay);
        Require(first.scheduler.executed_steps == replay.scheduler.executed_steps &&
                first.client.steps == replay.client.steps && first.server.steps == replay.server.steps &&
                first.client_report == replay.client_report && first.server_report == replay.server_report,
                "same scheduling inputs produced a different deterministic replay");
        send_retries += first.client_report[2] + first.server_report[2];
        std::cout << "RPC quantum=" << quantum << " steps=" << first.scheduler.executed_steps
                  << " digest=0x" << std::hex << first.client_report[1] << std::dec
                  << " send-retries=" << first.client_report[2] + first.server_report[2]
                  << " receive-retries=" << first.client_report[3] + first.server_report[3] << '\n';
    }
    Require(send_retries > 0, "capacity-one queue backpressure was not exercised");
    CheckSuccessful(Execute(client, server, 7, 0, 0, false), false);

    const auto corrupt = Execute(client, server, 7, 7);
    Require(!Successful(corrupt) && corrupt.server.stop == SessionStop::Exited &&
            corrupt.server.exit_code == 114 && corrupt.server_report[5] == 114 &&
            corrupt.server_report[0] == 6 && corrupt.server_stdout.empty(),
            "corrupt request checksum did not take the server's exact failure path");
    Require(corrupt.scheduler.stop == SchedulerStop::BudgetExhausted &&
            corrupt.scheduler.executed_steps == kBudget,
            "peer retry after server rejection did not stop at aggregate budget");

    const auto false_success = Execute(client, server, 7, 0, 77);
    Require(false_success.client_stdout == kClientOutput && false_success.server_stdout == kServerOutput &&
            false_success.scheduler.stop == SchedulerStop::Completed &&
            false_success.client.exit_code == 77 && !Successful(false_success),
            "success-looking stdout concealed a failing guest exit code");
    std::cout << "Original compiled C RPC: 64 bidirectional transactions, replay, isolation, "
                 "backpressure, checksum rejection and exit-status checks passed\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--run-pair") {
            const auto client = ReadFile(argv[2]);
            const auto server = ReadFile(argv[3]);
            RunTests(client, server);
            return EXIT_SUCCESS;
        }
        const auto client = MakeElf(original_rpc::kClient);
        const auto server = MakeElf(original_rpc::kServer);
        if (argc == 4 && std::string_view(argv[1]) == "--emit-elf") {
            WriteFile(argv[2], client); WriteFile(argv[3], server);
            return EXIT_SUCCESS;
        }
        Require(argc == 1, "usage: [--emit-elf client.elf server.elf | --run-pair client.elf server.elf]");
        RunTests(client, server);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Original compiled C RPC failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
