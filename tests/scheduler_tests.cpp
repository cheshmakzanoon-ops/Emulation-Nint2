#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include "nint2/scheduler.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using nint2::Access;
using nint2::GuestMemory;

bool InstallCode(GuestMemory& memory, std::span<const std::uint32_t> instructions) {
    if (!memory.Map(0x1000, 4096, Access::Read | Access::Write)) return false;
    for (std::size_t i = 0; i < instructions.size(); ++i)
        if (!memory.Write32(0x1000 + i * 4, instructions[i])) return false;
    return memory.Protect(0x1000, 4096, Access::Read | Access::Execute);
}
} // namespace

int main() {
    using nint2::SchedulerStop;
    using nint2::SessionStop;
    using nint2::SyntheticSession;
    using nint2::SyntheticScheduler;
    using nint2::kernel::Right;
    using nint2::kernel::Error;

    // Encoded using Clang 17 from our two original .s fixtures. They reside
    // in distinct emulated address spaces with different guest programs.
    constexpr std::array<std::uint32_t, 13> sender_code = {
        0xd2800088, 0xd2800040, 0xd4000001, 0xd28000a8,
        0x10000121, 0xd28000a2, 0xd4000001, 0xd28000e8,
        0xd2800020, 0xd4000001, 0xd2800048, 0xd2800000,
        0xd4000001
    };
    constexpr std::array<std::uint32_t, 16> receiver_code = {
        0xd28000c8, 0xd2800020, 0xd2860001, 0xd28000a2,
        0xd4000001, 0xb1002c1f, 0x54000061, 0xd2800020,
        0x17fffffc, 0xaa0003e2, 0xd2800028, 0xd2800020,
        0xd4000001, 0xd2800048, 0xd2800000, 0xd4000001
    };

    GuestMemory sender_memory(0x10000);
    GuestMemory receiver_memory(0x10000);
    EXPECT(InstallCode(sender_memory, sender_code));
    EXPECT(InstallCode(receiver_memory, receiver_code));
    EXPECT(receiver_memory.Map(0x3000, 4096, Access::Read | Access::Write));
    EXPECT(sender_memory.Map(0x3000, 4096, Access::Read | Access::Write));
    // Original sender's ADR addresses five bytes after its 52-byte code.
    constexpr std::array<std::uint8_t, 5> message{'P','I','N','G','!'};
    for (std::size_t i = 0; i < message.size(); ++i) {
        // RX guest code must be initialized before page protection: create
        // source data in an independent writable page and map its bytes.
        // The instruction fetch page already is RX, so direct changes fail.
        EXPECT(!sender_memory.Write8(0x1034 + i, message[i]));
    }
    // Use a fresh memory space for the sender to populate its image as if it
    // came from an original read-only ELF segment before final protection.
    GuestMemory sender_image(0x10000);
    EXPECT(sender_image.Map(0x1000, 4096, Access::Read | Access::Write));
    for (std::size_t i = 0; i < sender_code.size(); ++i)
        EXPECT(sender_image.Write32(0x1000 + 4 * i, sender_code[i]));
    for (std::size_t i = 0; i < message.size(); ++i)
        EXPECT(sender_image.Write8(0x1034 + i, message[i]));
    EXPECT(sender_image.Protect(0x1000, 4096, Access::Read | Access::Execute));
    EXPECT(sender_image.Map(0x3000, 4096, Access::Read | Access::Write));
    EXPECT(sender_image.Write8(0x3000, 'S'));
    SyntheticSession sender(sender_image, 0x1000, 0x4000);
    SyntheticSession receiver(receiver_memory, 0x1000, 0x4000);

    // The sender guest creates the queue with SVC #4; a host-side controller
    // attenuates its authority when admitting the other guest, no transfer SVC.
    const auto created = sender.Run(3);
    EXPECT(created.stop == SessionStop::StepLimit && created.steps == 3);
    EXPECT(sender.Context().x[0] == 1 && sender.HandleCount() == 1);
    const auto granted = sender.GrantQueueTo(1, receiver, Right::Receive);
    EXPECT(granted.Ok() && granted.value == 1);
    EXPECT(receiver.HandleCount() == 1);
    EXPECT(receiver.GrantQueueTo(1, sender, Right::Send).error == Error::PermissionDenied);
    EXPECT(sender.GrantQueueTo(1, receiver, Right::None).error == Error::PermissionDenied);
    EXPECT(receiver_memory.Read8(0x3000) == 0);
    EXPECT(sender_image.Read8(0x3000) == 'S');

    // Intentionally start receiver before sender's QueueSend: the first read
    // returns EAGAIN, and its own A64 retry loop must subsequently recover.
    const auto empty = receiver.Run(5);
    EXPECT(empty.stop == SessionStop::StepLimit && empty.steps == 5);
    EXPECT(receiver.Context().x[0] == static_cast<std::uint64_t>(-11LL));
    EXPECT(receiver_memory.Read8(0x3000) == 0);

    SyntheticScheduler scheduler;
    EXPECT(scheduler.Add(sender) && scheduler.Add(receiver));
    EXPECT(!scheduler.Add(sender));
    EXPECT(scheduler.Size() == 2);
    EXPECT(scheduler.Run(1).stop == SchedulerStop::BudgetExhausted);
    const auto completed = scheduler.Run(128, 1);
    EXPECT(completed.stop == SchedulerStop::Completed);
    EXPECT(completed.process_count == 2);
    EXPECT(completed.exited == 2 && completed.failed == 0);
    EXPECT(sender.HandleCount() == 0);
    EXPECT(receiver.HandleCount() == 1);  // recipient grant survives source close
    EXPECT(sender.Stdout().empty());
    EXPECT(receiver.Stdout() == "PING!");
    EXPECT(receiver_memory.Read8(0x3000) == 'P');
    EXPECT(receiver_memory.Read8(0x3004) == '!');
    EXPECT(sender_image.Read8(0x3000) == 'S');  // no shared memory
    EXPECT(sender.Context().pc == 0x1030);
    EXPECT(receiver.Context().pc == 0x103c);
    EXPECT(scheduler.Run(5).stop == SchedulerStop::Completed); // idempotent
    EXPECT(!scheduler.Add(sender));

    // Synthetic time-slicing of tight loops must be fair, budget-bounded and
    // resumable across calls; no dependence on host clocks or OS threads.
    GuestMemory loop_a(0x10000), loop_b(0x10000);
    constexpr std::array<std::uint32_t, 1> loop{0x14000000}; // B PC
    EXPECT(InstallCode(loop_a, loop) && InstallCode(loop_b, loop));
    SyntheticSession a(loop_a, 0x1000, 0x4000);
    SyntheticSession b(loop_b, 0x1000, 0x4000);
    SyntheticScheduler fair;
    EXPECT(fair.Run(1).stop == SchedulerStop::InvalidConfiguration);
    EXPECT(fair.Add(a) && fair.Add(b));
    EXPECT(fair.Run(4, 0).stop == SchedulerStop::InvalidConfiguration);
    EXPECT(a.TotalSteps() == 0 && b.TotalSteps() == 0);
    auto sliced = fair.Run(7, 2);
    EXPECT(sliced.stop == SchedulerStop::BudgetExhausted);
    EXPECT(sliced.executed_steps == 7);
    EXPECT(a.TotalSteps() == 4 && b.TotalSteps() == 3);
    sliced = fair.Run(1);
    EXPECT(sliced.executed_steps == 8);
    EXPECT(a.TotalSteps() == 5 && b.TotalSteps() == 3);

    // One fault must not prevent a separate healthy process from finishing.
    GuestMemory faulty_memory(0x10000);
    constexpr std::array<std::uint32_t, 1> undefined{0xffffffff};
    EXPECT(InstallCode(faulty_memory, undefined));
    SyntheticSession faulty(faulty_memory, 0x1000, 0x4000);
    SyntheticScheduler fail_scheduler;
    EXPECT(fail_scheduler.Add(faulty) && fail_scheduler.Add(sender));
    const auto failed = fail_scheduler.Run(10);
    EXPECT(failed.stop == SchedulerStop::GuestFailed);
    EXPECT(failed.failed == 1 && failed.exited == 1);

    std::cout << "Synthetic two-AArch64-process IPC, fair scheduling and faults passed\n";
    return EXIT_SUCCESS;
}
