#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>
#include "nint2/elf.hpp"
#include "nint2/scheduler.hpp"

namespace {
void Put(std::vector<std::uint8_t>& bytes, std::size_t position,
         std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        bytes[position + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> OriginalElf(std::span<const std::uint32_t> words,
                                      std::span<const std::uint8_t> data = {}) {
    // Static ELF64/AArch64 ET_EXEC containing only our original A64 code.
    // A read+execute PT_LOAD at 0x1000; data follows the instruction stream.
    constexpr std::size_t kOffset = 0x100;
    const auto file_bytes = words.size() * 4 + data.size();
    std::vector<std::uint8_t> elf(kOffset + file_bytes);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2; elf[5] = 1; elf[6] = 1;
    Put(elf, 16, 2, 2); Put(elf, 18, 183, 2);
    Put(elf, 20, 1, 4); Put(elf, 24, 0x1000, 8);
    Put(elf, 32, 64, 8); Put(elf, 52, 64, 2);
    Put(elf, 54, 56, 2); Put(elf, 56, 1, 2);
    Put(elf, 64, 1, 4); Put(elf, 68, 5, 4);
    Put(elf, 72, kOffset, 8); Put(elf, 80, 0x1000, 8);
    Put(elf, 96, file_bytes, 8); Put(elf, 104, file_bytes, 8);
    Put(elf, 112, 0x100, 8);
    for (std::size_t i = 0; i < words.size(); ++i)
        Put(elf, kOffset + 4 * i, words[i], 4);
    for (std::size_t i = 0; i < data.size(); ++i)
        elf[kOffset + 4 * words.size() + i] = data[i];
    return elf;
}

// Encodings independently assembled from tests/fixtures/original_sender.s and
// original_receiver.s. No platform firmware, keys, device code or libraries.
constexpr std::array<std::uint32_t, 13> kSender{
    0xd2800088, 0xd2800040, 0xd4000001, 0xd28000a8,
    0x10000121, 0xd28000a2, 0xd4000001, 0xd28000e8,
    0xd2800020, 0xd4000001, 0xd2800048, 0xd2800000,
    0xd4000001
};
constexpr std::array<std::uint32_t, 16> kReceiver{
    0xd28000c8, 0xd2800020, 0xd2860001, 0xd28000a2,
    0xd4000001, 0xb1002c1f, 0x54000061, 0xd2800020,
    0x17fffffc, 0xaa0003e2, 0xd2800028, 0xd2800020,
    0xd4000001, 0xd2800048, 0xd2800000, 0xd4000001
};
constexpr std::array<std::uint8_t, 5> kMessage{'P','I','N','G','!'};

bool WriteFile(const char* path, std::span<const std::uint8_t> content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file.write(reinterpret_cast<const char*>(content.data()),
                      static_cast<std::streamsize>(content.size())).good();
}
std::vector<std::uint8_t> ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 32 * 1024 * 1024) return {};
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<std::uint8_t> data(size);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(size))) return {};
    return data;
}

bool ExecutePair(std::span<const std::uint8_t> producer_elf,
                 std::span<const std::uint8_t> consumer_elf) {
    using nint2::Access;
    using nint2::ElfError;
    using nint2::GuestMemory;
    using nint2::SchedulerStop;
    using nint2::SessionStop;
    using nint2::SyntheticScheduler;
    using nint2::SyntheticSession;
    using nint2::kernel::Right;
    const auto fail = [](const char* reason) {
        std::cerr << "Two-ELF integration failed: " << reason << '\n';
        return false;
    };
    GuestMemory producer_memory(0x400000);
    GuestMemory consumer_memory(0x400000);
    const auto producer_entry = nint2::LoadElf64(producer_elf, producer_memory);
    const auto consumer_entry = nint2::LoadElf64(consumer_elf, consumer_memory);
    if (producer_entry.error != ElfError::None || consumer_entry.error != ElfError::None)
        return fail("ELF loader rejected one of the original programs");
    if (!producer_memory.Map(0x3000, 4096, Access::Read | Access::Write) ||
        !consumer_memory.Map(0x3000, 4096, Access::Read | Access::Write))
        return fail("private guest data mapping");
    if (!producer_memory.Write8(0x3000, 'S')) return fail("initializing source memory");
    SyntheticSession producer(producer_memory, producer_entry.entry, 0x4000);
    SyntheticSession consumer(consumer_memory, consumer_entry.entry, 0x4000);

    // A real AArch64 SVC creates the producer queue. The test host grants a
    // receive-only capability to the other isolated process (not a guest ABI).
    const auto setup = producer.Run(3);
    if (setup.stop != SessionStop::StepLimit || setup.steps != 3 ||
        producer.Context().x[0] != 1) return fail("producer queue creation");
    const auto granted = producer.GrantQueueTo(1, consumer, Right::Receive);
    if (!granted.Ok() || granted.value != 1) return fail("attenuated capability grant");
    // Force an early empty-queue read to verify EAGAIN, the guest's retry loop,
    // and subsequent recovery without modifying the private destination.
    const auto empty = consumer.Run(5);
    if (empty.stop != SessionStop::StepLimit ||
        consumer.Context().x[0] != static_cast<std::uint64_t>(-11LL) ||
        consumer_memory.Read8(0x3000) != 0)
        return fail("nonblocking receive did not report EAGAIN");

    SyntheticScheduler scheduler;
    if (!scheduler.Add(producer) || !scheduler.Add(consumer))
        return fail("scheduler registration");
    const auto result = scheduler.Run(256);
    if (result.stop != SchedulerStop::Completed || result.exited != 2 ||
        result.failed != 0) return fail("not all guest processes exited cleanly");
    if (consumer.Stdout() != "PING!" || !producer.Stdout().empty())
        return fail("IPC payload or output mismatch");
    if (producer.HandleCount() != 0 || consumer.HandleCount() != 1 ||
        producer_memory.Read8(0x3000) != 'S' ||
        consumer_memory.Read8(0x3000) != 'P' ||
        consumer_memory.Read8(0x3004) != '!')
        return fail("guest isolation, handle lifetime or received bytes");
    std::cout << "Two original AArch64 ELFs exchanged PING! via synthetic IPC ("
              << result.executed_steps << " scheduled steps)\n";
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--emit-elf") {
        const auto sender = OriginalElf(kSender, kMessage);
        const auto receiver = OriginalElf(kReceiver);
        return WriteFile(argv[2], sender) && WriteFile(argv[3], receiver)
               ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--run-pair") {
        const auto sender = ReadFile(argv[2]);
        const auto receiver = ReadFile(argv[3]);
        if (sender.empty() || receiver.empty()) return EXIT_FAILURE;
        return ExecutePair(sender, receiver) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1) return EXIT_FAILURE;
    const auto sender = OriginalElf(kSender, kMessage);
    const auto receiver = OriginalElf(kReceiver);
    return ExecutePair(sender, receiver) ? EXIT_SUCCESS : EXIT_FAILURE;
}
