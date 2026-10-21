#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/session.hpp"

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

namespace {
bool Install(nint2::GuestMemory& memory) {
    using nint2::Access;
    return memory.Map(0x1000, 4096, Access::Read | Access::Write) &&
           memory.Write32(0x1000, 0xd4000001) && // SVC #0; registers set by host
           memory.Protect(0x1000, 4096, Access::Read | Access::Execute) &&
           memory.Map(0x2000, 4096, Access::Read | Access::Write);
}

bool Call(nint2::SyntheticSession& session, std::uint64_t number,
          std::uint64_t a, std::uint64_t b, std::uint64_t c,
          std::uint64_t expected) {
    auto& registers = session.Context();
    registers.pc = 0x1000;
    registers.x[8] = number;
    registers.x[0] = a;
    registers.x[1] = b;
    registers.x[2] = c;
    return session.Run(1).stop == nint2::SessionStop::StepLimit &&
           registers.x[0] == expected;
}
} // namespace

int main() {
    using nint2::GuestMemory;
    using nint2::SyntheticSession;
    using nint2::kernel::Error;
    using nint2::kernel::HandleTable;
    using nint2::kernel::Right;
    constexpr auto denied = static_cast<std::uint64_t>(-13LL);
    constexpr auto invalid = static_cast<std::uint64_t>(-9LL);
    GuestMemory sender_memory(0x10000), receiver_memory(0x10000);
    EXPECT(Install(sender_memory) && Install(receiver_memory));
    SyntheticSession sender(sender_memory, 0x1000, 0x3000);
    SyntheticSession receiver(receiver_memory, 0x1000, 0x3000);

    // The bootstrap owner's ID differs from both process-local imported IDs.
    HandleTable bootstrap;
    EXPECT(bootstrap.CreateQueue(1).value == 1);
    EXPECT(bootstrap.CreateQueue(1).value == 2);
    EXPECT(sender.ImportQueue(bootstrap, 0, Right::Send).error == Error::InvalidHandle);
    EXPECT(sender.ImportQueue(bootstrap, 99, Right::Send).error == Error::InvalidHandle);
    EXPECT(sender.ImportQueue(bootstrap, 2, Right::None).error == Error::PermissionDenied);
    EXPECT(sender.ImportQueue(bootstrap, 2, static_cast<Right>(0x80)).error == Error::PermissionDenied);
    EXPECT(sender.HandleCount() == 0 && sender.TotalSteps() == 0);
    EXPECT(sender.Context().pc == 0x1000 && sender.Context().x[0] == 0);

    const auto send = sender.ImportQueue(bootstrap, 2, Right::Send);
    const auto receive = receiver.ImportQueue(bootstrap, 2, Right::Receive);
    EXPECT(send.Ok() && send.value == 1 && receive.Ok() && receive.value == 1);
    EXPECT(sender.HandleCount() == 1 && receiver.HandleCount() == 1);
    EXPECT(bootstrap.Size() == 2);
    EXPECT(sender.TotalSteps() == 0 && receiver.TotalSteps() == 0);
    EXPECT(sender.GrantQueueTo(1, receiver, Right::Send).error == Error::PermissionDenied);
    EXPECT(receiver.GrantQueueTo(1, sender, Right::Receive).error == Error::PermissionDenied);

    // Even a duplicable owner capability cannot grant rights it does not own.
    const auto limited = bootstrap.Duplicate(2, Right::Send | Right::Duplicate);
    EXPECT(limited.Ok());
    const auto limited_id = static_cast<std::uint32_t>(limited.value);
    EXPECT(sender.ImportQueue(bootstrap, limited_id, Right::Receive).error == Error::PermissionDenied);
    const auto no_duplicate = bootstrap.Duplicate(2, Right::Send);
    EXPECT(no_duplicate.Ok());
    EXPECT(sender.ImportQueue(bootstrap, static_cast<std::uint32_t>(no_duplicate.value),
                              Right::Send).error == Error::PermissionDenied);
    EXPECT(sender.HandleCount() == 1 && receiver.HandleCount() == 1);

    // Closing every owner alias must leave only the separately imported queue
    // references alive. The host retains no shared_ptr to mask a lifetime bug.
    EXPECT(bootstrap.Close(1).Ok() && bootstrap.Close(2).Ok());
    EXPECT(bootstrap.Close(limited_id).Ok());
    EXPECT(bootstrap.Close(static_cast<std::uint32_t>(no_duplicate.value)).Ok());
    EXPECT(bootstrap.Size() == 0);
    EXPECT(sender.ImportQueue(bootstrap, 2, Right::Send).error == Error::InvalidHandle);
    EXPECT(sender_memory.Write8(0x2000, 'Q'));
    EXPECT(receiver_memory.Write8(0x2000, '?'));

    // Same numeric IDs do not imply global authority or shared guest memory.
    EXPECT(Call(sender, 5, 2, 0x2000, 1, invalid));
    EXPECT(Call(receiver, 5, 1, 0x2000, 1, denied));
    EXPECT(Call(sender, 6, 1, 0x2000, 1, denied));
    EXPECT(Call(sender, 5, 1, 0x2000, 1, 1));
    EXPECT(receiver_memory.Read8(0x2000) == '?');
    EXPECT(Call(sender, 7, 1, 0, 0, 0));
    EXPECT(sender.HandleCount() == 0 && receiver.HandleCount() == 1);
    EXPECT(Call(receiver, 6, 1, 0x2000, 1, 1));
    EXPECT(receiver_memory.Read8(0x2000) == 'Q');
    EXPECT(sender_memory.Read8(0x2000) == 'Q');
    EXPECT(Call(receiver, 7, 1, 0, 0, 0));
    EXPECT(receiver.HandleCount() == 0);

    // Recipient exhaustion leaves the owner and all earlier imports intact.
    const auto next_owner = bootstrap.CreateQueue(1);
    EXPECT(next_owner.Ok() && next_owner.value > no_duplicate.value);
    const auto next_id = static_cast<std::uint32_t>(next_owner.value);
    for (std::size_t i = 0; i < HandleTable::kDefaultCapacity; ++i) {
        const auto imported = receiver.ImportQueue(bootstrap, next_id, Right::Receive);
        EXPECT(imported.Ok() && imported.value == i + 2);
    }
    EXPECT(receiver.HandleCount() == HandleTable::kDefaultCapacity);
    EXPECT(receiver.ImportQueue(bootstrap, next_id, Right::Receive).error == Error::Exhausted);
    EXPECT(bootstrap.Size() == 1 && bootstrap.Check(next_id, Right::Duplicate) == Error::None);
    EXPECT(Call(receiver, 7, 2, 0, 0, 0));
    const auto recovered = receiver.ImportQueue(bootstrap, next_id, Right::Receive);
    EXPECT(recovered.Ok() && recovered.value == HandleTable::kDefaultCapacity + 2);
    EXPECT(receiver.HandleCount() == HandleTable::kDefaultCapacity);
    EXPECT(Call(receiver, 6, 2, 0x2000, 1, invalid)); // closed IDs stay stale

    std::cout << "Host queue imports preserve scoped rights, lifetime and capacity\n";
    return EXIT_SUCCESS;
}
