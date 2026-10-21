#include "nint2/scheduler.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>

#define REQUIRE(condition) do { if (!(condition)) { \
    std::cerr << "FAILED " << #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
using namespace nint2;
using nint2::kernel::Right;

// Original independent AArch64 source: tests/fixtures/wait_sender.s and
// tests/fixtures/wait_receiver.s, assembled with Clang for AArch64.
// All instruction words here are original and do not contain console code.
constexpr std::array<std::uint32_t, 20> kSender{
    0xd2800128, 0xaa1303e0, 0xd2800021, 0xd2800022, 0xd4000001,
    0xf100001f, 0x54000161, 0xd28000a8, 0xaa1303e0, 0x10000161,
    0xd28000a2, 0xd4000001, 0xf100141f, 0x54000081, 0xd2800048,
    0xd2800000, 0xd4000001, 0xd2800048, 0xd2800900, 0xd4000001
};
constexpr std::array<std::uint32_t, 25> kReceiver{
    0xd2800128, 0xaa1303e0, 0xd2800041, 0xd2800022, 0xd4000001,
    0xf100001f, 0x54000201, 0xd28000c8, 0xaa1303e0, 0xaa1403e1,
    0xd28000a2, 0xd4000001, 0xf100141f, 0x54000121, 0xd2800028,
    0xd2800020, 0xaa1403e1, 0xd28000a2, 0xd4000001, 0xd2800048,
    0xd2800000, 0xd4000001, 0xd2800048, 0xd28008e0, 0xd4000001
};
constexpr std::array<std::uint8_t, 5> kMessage{'W','A','K','E','!'};

template<std::size_t N>
bool Install(GuestMemory& memory, const std::array<std::uint32_t, N>& words,
             bool sender = false) {
    if (!memory.Map(0x1000, 4096, Access::Read | Access::Write)) return false;
    for (std::size_t i = 0; i < words.size(); ++i)
        if (!memory.Write32(0x1000 + i * 4, words[i])) return false;
    if (sender)
        for (std::size_t i = 0; i < kMessage.size(); ++i)
            if (!memory.Write8(0x1050 + i, kMessage[i])) return false;
    return memory.Protect(0x1000, 4096, Access::Read | Access::Execute) &&
           memory.Map(0x3000, 4096, Access::Read | Access::Write);
}

void Bind(SyntheticSession& session, std::uint32_t queue_handle,
          bool receive) {
    session.Context().x[19] = queue_handle;
    session.Context().x[20] = 0x3000;
    (void)receive;
}

bool Matches(const GuestMemory& memory, std::uint64_t address) {
    for (std::size_t i = 0; i < kMessage.size(); ++i)
        if (memory.Read8(address + i) != kMessage[i]) return false;
    return true;
}

bool InstallSvc(GuestMemory& memory) {
    return memory.Map(0x1000, 4096, Access::Read | Access::Write) &&
           memory.Write32(0x1000, 0xd4000001) &&
           memory.Protect(0x1000, 4096, Access::Read | Access::Execute);
}
}

int main() {
    using namespace nint2;
    using namespace nint2::kernel;

    // A running AArch64 sender wakes a blocked receiver without host threads,
    // real time, unbounded polling or a special-case code path.
    GuestMemory client_mem(0x10000), server_mem(0x10000);
    REQUIRE(Install(client_mem, kReceiver));
    REQUIRE(Install(server_mem, kSender, true));
    SyntheticSession receiver(client_mem, 0x1000, 0x4000);
    SyntheticSession sender(server_mem, 0x1000, 0x4000);
    HandleTable bootstrap;
    REQUIRE(bootstrap.CreateQueue(1).Ok());
    const auto reader = receiver.ImportQueue(bootstrap, 1, Right::Receive);
    const auto writer = sender.ImportQueue(bootstrap, 1, Right::Send);
    REQUIRE(reader.Ok() && writer.Ok());
    Bind(receiver, static_cast<std::uint32_t>(reader.value), true);
    Bind(sender, static_cast<std::uint32_t>(writer.value), false);
    REQUIRE(bootstrap.Close(1).Ok());

    SyntheticScheduler scheduler;
    REQUIRE(scheduler.Add(receiver) && scheduler.Add(sender));
    const auto finished = scheduler.Run(200, 1);
    REQUIRE(finished.stop == SchedulerStop::Completed);
    REQUIRE(finished.exited == 2 && finished.failed == 0);
    REQUIRE(receiver.Run(0).exit_code == 0 && sender.Run(0).exit_code == 0);
    REQUIRE(receiver.Stdout() == "WAKE!");
    REQUIRE(Matches(client_mem, 0x3000));
    REQUIRE(receiver.TotalSteps() < 40 && sender.TotalSteps() < 30);
    REQUIRE(receiver.HandleCount() == 1 && sender.HandleCount() == 1);
    REQUIRE(sender.CanResume()); // terminal sessions are not blocked waiters
    REQUIRE(scheduler.Run(1).stop == SchedulerStop::Completed);

    // A single waiter blocked on a still-open producer returns Deadlock
    // deterministically. A host message then resumes the *same* A64 SVC.
    GuestMemory stalled_memory(0x10000);
    REQUIRE(Install(stalled_memory, kReceiver));
    SyntheticSession stalled(stalled_memory, 0x1000, 0x4000);
    HandleTable host;
    REQUIRE(host.CreateQueue(1).Ok());
    auto inbound = stalled.ImportQueue(host, 1, Right::Receive);
    REQUIRE(inbound.Ok());
    Bind(stalled, static_cast<std::uint32_t>(inbound.value), true);
    SyntheticScheduler isolated;
    REQUIRE(isolated.Add(stalled));
    REQUIRE(isolated.Run(4, 1).stop == SchedulerStop::BudgetExhausted);
    const auto stuck = isolated.Run(50, 1);
    REQUIRE(stuck.stop == SchedulerStop::Deadlock);
    REQUIRE(stuck.executed_steps == 5);
    REQUIRE(stalled.Context().pc == 0x1010);
    REQUIRE(!stalled.CanResume());
    const auto q = host.Get(1, Right::Send);
    REQUIRE(q && q->Send(kMessage).Ok());
    REQUIRE(stalled.CanResume());
    const auto resumed = isolated.Run(100, 1);
    REQUIRE(resumed.stop == SchedulerStop::Completed && resumed.exited == 1);
    REQUIRE(stalled.Run(0).exit_code == 0 && stalled.Stdout() == "WAKE!");
    REQUIRE(Matches(stalled_memory, 0x3000));

    // A process that loses its last sender does not wait forever. Its wait
    // returns -EPIPE and our original guest exits through the error branch.
    GuestMemory orphan_memory(0x10000);
    REQUIRE(Install(orphan_memory, kReceiver));
    SyntheticSession orphan(orphan_memory, 0x1000, 0x4000);
    HandleTable orphan_owner;
    REQUIRE(orphan_owner.CreateQueue(1).Ok());
    auto orphan_handle = orphan.ImportQueue(orphan_owner, 1, Right::Receive);
    REQUIRE(orphan_handle.Ok());
    Bind(orphan, static_cast<std::uint32_t>(orphan_handle.value), true);
    REQUIRE(orphan_owner.Close(1).Ok());
    SyntheticScheduler orphan_sched;
    REQUIRE(orphan_sched.Add(orphan));
    const auto lost = orphan_sched.Run(100, 1);
    REQUIRE(lost.stop == SchedulerStop::Completed);
    REQUIRE(lost.exited == 1 && orphan.Run(0).exit_code == 71);
    REQUIRE(orphan.Stdout().empty());

    // When the last reader retires, blocked senders wake with an explicit
    // peer-closed status instead of stalling. Saturated queues also wake
    // correctly when a host consumer removes the message.
    GuestMemory full_memory(0x10000);
    REQUIRE(Install(full_memory, kSender, true));
    SyntheticSession full_sender(full_memory, 0x1000, 0x4000);
    HandleTable full_owner, sink;
    REQUIRE(full_owner.CreateQueue(1).Ok());
    const auto hwrite = full_sender.ImportQueue(full_owner, 1, Right::Send);
    const auto hread = full_owner.GrantTo(1, sink, Right::Receive);
    REQUIRE(hwrite.Ok() && hread.Ok());
    Bind(full_sender, static_cast<std::uint32_t>(hwrite.value), false);
    const auto fullq = full_owner.Get(1, Right::Receive);
    REQUIRE(fullq && fullq->Send(kMessage).Ok());
    REQUIRE(full_owner.Close(1).Ok());
    SyntheticScheduler full_scheduler;
    REQUIRE(full_scheduler.Add(full_sender));
    REQUIRE(full_scheduler.Run(100, 1).stop == SchedulerStop::Deadlock);
    REQUIRE(!full_sender.CanResume());
    REQUIRE(fullq->Consume().Ok());
    REQUIRE(full_sender.CanResume());
    REQUIRE(full_scheduler.Run(100, 1).stop == SchedulerStop::Completed);
    REQUIRE(full_sender.Run(0).exit_code == 0);
    REQUIRE(fullq->Depth() == 1);
    std::array<std::uint8_t, 5> buffered{};
    REQUIRE(fullq->Peek(buffered).Ok() && buffered == kMessage);

    GuestMemory peer_memory(0x10000);
    REQUIRE(Install(peer_memory, kSender, true));
    SyntheticSession abandoned(peer_memory, 0x1000, 0x4000);
    HandleTable pair_owner, peer_sink;
    REQUIRE(pair_owner.CreateQueue(1).Ok());
    const auto ah = abandoned.ImportQueue(pair_owner, 1, Right::Send);
    REQUIRE(pair_owner.GrantTo(1, peer_sink, Right::Receive).Ok());
    REQUIRE(ah.Ok());
    Bind(abandoned, static_cast<std::uint32_t>(ah.value), false);
    REQUIRE(pair_owner.Get(1, Right::Send)->Send(kMessage).Ok());
    REQUIRE(pair_owner.Close(1).Ok());
    SyntheticScheduler peer_sched;
    REQUIRE(peer_sched.Add(abandoned));
    REQUIRE(peer_sched.Run(100, 1).stop == SchedulerStop::Deadlock);
    peer_sink.Retire();
    REQUIRE(abandoned.CanResume());
    REQUIRE(peer_sched.Run(100, 1).stop == SchedulerStop::Completed);
    REQUIRE(abandoned.Run(0).exit_code == 72);

    // Poll and validation cases: no blocking, exact error values, unchanged
    // program counter on true block and no host pointer dereferencing.
    GuestMemory probe_memory(0x10000);
    REQUIRE(InstallSvc(probe_memory));
    SyntheticSession probe(probe_memory, 0x1000, 0x2000);
    HandleTable probe_owner;
    REQUIRE(probe_owner.CreateQueue(1).Ok());
    auto probe_read = probe.ImportQueue(probe_owner, 1, Right::Receive);
    REQUIRE(probe_read.Ok());
    auto& regs = probe.Context();
    auto invoke = [&](std::uint64_t handle, std::uint64_t direction,
                      std::uint64_t mode) {
        regs.pc = 0x1000;
        regs.x[0] = handle; regs.x[1] = direction;
        regs.x[2] = mode; regs.x[8] = 9;
        return probe.Run(1);
    };
    REQUIRE(invoke(probe_read.value, 2, 0).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == static_cast<std::uint64_t>(-11LL));
    REQUIRE(regs.pc == 0x1004);
    REQUIRE(invoke(probe_read.value, 1, 0).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == static_cast<std::uint64_t>(-13LL));
    REQUIRE(invoke(0, 2, 0).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == static_cast<std::uint64_t>(-9LL));
    REQUIRE(invoke(probe_read.value, 3, 0).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(invoke(probe_read.value, 2, 2).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(invoke(probe_read.value, 2, 1).stop == SessionStop::Blocked);
    REQUIRE(regs.pc == 0x1000 && !probe.CanResume());
    const auto before = probe.TotalSteps();
    REQUIRE(probe.Run(100).stop == SessionStop::Blocked);
    REQUIRE(probe.TotalSteps() == before);
    REQUIRE(probe_owner.Get(1, Right::Send)->Send(kMessage).Ok());
    REQUIRE(probe.CanResume());
    REQUIRE(probe.Run(1).stop == SessionStop::StepLimit);
    REQUIRE(regs.x[0] == 0 && regs.pc == 0x1004);

    // Byte-exact send waits: 64 queue slots remain available, but only one
    // byte of the 64 KiB byte quota is free, so a two-byte Send must suspend.
    GuestMemory exact_mem(0x10000);
    REQUIRE(InstallSvc(exact_mem));
    SyntheticSession exact_guest(exact_mem, 0x1000, 0x2000);
    HandleTable exact_owner;
    REQUIRE(exact_owner.CreateQueue(64).Ok());
    const auto exact_fd = exact_guest.ImportQueue(exact_owner, 1, Right::Send);
    REQUIRE(exact_fd.Ok());
    auto quota = exact_owner.Get(1, Right::Send);
    REQUIRE(quota != nullptr);
    std::array<std::uint8_t, MessageQueue::kMaxMessage> large{};
    for (unsigned i = 0; i < 15; ++i) REQUIRE(quota->Send(large).Ok());
    std::array<std::uint8_t, MessageQueue::kMaxMessage - 1> nearly_full{};
    REQUIRE(quota->Send(nearly_full).Ok());
    REQUIRE(quota->Depth() == 16);
    REQUIRE(quota->QueuedBytes() == MessageQueue::kMaxQueuedBytes - 1);
    auto& x = exact_guest.Context();
    auto query = [&](std::uint64_t direction, std::uint64_t mode,
                     std::uint64_t length) {
        x.pc = 0x1000;
        x.x[0] = exact_fd.value; x.x[1] = direction;
        x.x[2] = mode; x.x[3] = length; x.x[8] = 9;
        return exact_guest.Run(1);
    };
    REQUIRE(query(1, 0, 1).stop == SessionStop::StepLimit);
    REQUIRE(x.x[0] == 0);
    REQUIRE(query(1, 0, 2).stop == SessionStop::StepLimit);
    REQUIRE(x.x[0] == static_cast<std::uint64_t>(-11LL));
    REQUIRE(query(1, 0, 4097).stop == SessionStop::StepLimit);
    REQUIRE(x.x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(query(2, 0, 1).stop == SessionStop::StepLimit);
    REQUIRE(x.x[0] == static_cast<std::uint64_t>(-22LL));
    REQUIRE(query(1, 1, 2).stop == SessionStop::Blocked);
    REQUIRE(!exact_guest.CanResume() && x.pc == 0x1000);
    const auto frozen_steps = exact_guest.TotalSteps();
    REQUIRE(exact_guest.Run(32).stop == SessionStop::Blocked);
    REQUIRE(exact_guest.TotalSteps() == frozen_steps);
    REQUIRE(quota->Consume().value == MessageQueue::kMaxMessage);
    REQUIRE(exact_guest.CanResume());
    REQUIRE(exact_guest.Run(1).stop == SessionStop::StepLimit);
    REQUIRE(x.x[0] == 0 && x.pc == 0x1004);
    REQUIRE(exact_owner.Close(1).Ok());

    std::cout << "Original A64 blocking IPC, peer loss, deadlock and poll tests passed\n";
    return EXIT_SUCCESS;
}
