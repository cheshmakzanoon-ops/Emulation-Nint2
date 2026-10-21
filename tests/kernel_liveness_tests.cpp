#include "nint2/kernel.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "FAIL " << #expression << " at " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using namespace nint2::kernel;
    using nint2::kernel::Error;
    const std::array<std::uint8_t, 3> packet{1, 2, 3};

    // A newly created queue has exactly one dual-direction capability.
    HandleTable owner;
    const auto created = owner.CreateQueue(2);
    CHECK(created.Ok() && created.value == 1);
    auto queue = owner.Get(1, Right::Send);
    CHECK(queue != nullptr && queue->SenderCount() == 1);
    CHECK(queue->ReceiverCount() == 1);
    CHECK(owner.Status(1, Right::Send).Ok());
    CHECK(owner.Status(1, Right::Receive).error == Error::Empty);
    CHECK(owner.Status(1, Right::Duplicate).error == Error::InvalidArgument);
    CHECK(owner.Status(0, Right::Send).error == Error::InvalidHandle);

    // Duplicates count as distinct endpoint references, even with shared data.
    const auto send = owner.Duplicate(1, Right::Send);
    const auto receive = owner.Duplicate(1, Right::Receive);
    const auto duplicate_only = owner.Duplicate(1, Right::Duplicate);
    CHECK(send.Ok() && receive.Ok() && duplicate_only.Ok());
    CHECK(queue->SenderCount() == 2 && queue->ReceiverCount() == 2);
    CHECK(owner.Close(static_cast<std::uint32_t>(duplicate_only.value)).Ok());
    CHECK(queue->SenderCount() == 2 && queue->ReceiverCount() == 2);
    CHECK(owner.Close(1).Ok());
    CHECK(queue->SenderCount() == 1 && queue->ReceiverCount() == 1);
    CHECK(owner.Status(static_cast<std::uint32_t>(send.value), Right::Receive).error ==
          Error::PermissionDenied);
    CHECK(queue->Send(packet).Ok());
    CHECK(owner.Status(static_cast<std::uint32_t>(receive.value), Right::Receive).Ok());
    CHECK(owner.Close(static_cast<std::uint32_t>(send.value)).Ok());
    CHECK(queue->SenderCount() == 0 && queue->ReceiverCount() == 1);
    // Final buffered message remains available even when no sender survives.
    CHECK(owner.Status(static_cast<std::uint32_t>(receive.value), Right::Receive).Ok());
    std::array<std::uint8_t, 3> received{};
    CHECK(queue->Peek(received).Ok() && received == packet);
    CHECK(queue->Consume().Ok());
    CHECK(owner.Status(static_cast<std::uint32_t>(receive.value), Right::Receive).error ==
          Error::PeerClosed);
    CHECK(owner.Close(static_cast<std::uint32_t>(receive.value)).Ok());
    CHECK(queue->ReceiverCount() == 0);
    CHECK(queue->Readiness(Right::Send).error == Error::PeerClosed);

    // An attenuated cross-table grant keeps the exact original storage but
    // cannot inherit unrelated access rights or revive a retired process.
    HandleTable bootstrap;
    const auto host = bootstrap.CreateQueue(1);
    CHECK(host.Ok());
    HandleTable producer;
    HandleTable consumer;
    const auto send_only = bootstrap.GrantTo(1, producer, Right::Send);
    const auto receive_only = bootstrap.GrantTo(1, consumer, Right::Receive);
    CHECK(send_only.Ok() && receive_only.Ok());
    auto shared = bootstrap.Get(1, Right::Receive);
    CHECK(shared && shared->SenderCount() == 2 && shared->ReceiverCount() == 2);
    CHECK(bootstrap.Close(1).Ok());
    CHECK(shared->SenderCount() == 1 && shared->ReceiverCount() == 1);
    CHECK(producer.Status(1, Right::Send).Ok());
    CHECK(consumer.Status(1, Right::Receive).error == Error::Empty);
    producer.Retire();
    CHECK(!producer.Active());
    CHECK(producer.Size() == 1);
    CHECK(shared->SenderCount() == 0 && shared->ReceiverCount() == 1);
    CHECK(consumer.Status(1, Right::Receive).error == Error::PeerClosed);
    producer.Retire();
    CHECK(shared->SenderCount() == 0);
    CHECK(producer.Duplicate(1, Right::Send).error == Error::InvalidHandle);
    CHECK(producer.GrantTo(1, consumer, Right::Send).error == Error::InvalidHandle);
    CHECK(producer.Close(1).Ok()); // closing retired handles cannot underflow refs
    CHECK(shared->SenderCount() == 0);
    CHECK(consumer.Close(1).Ok());
    CHECK(shared->ReceiverCount() == 0);

    // Last receiver retires while writer still holds a valid send reference.
    HandleTable grantor;
    auto grant = grantor.CreateQueue(1);
    CHECK(grant.Ok());
    auto strong = grantor.Get(1, Right::Send);
    HandleTable writer;
    HandleTable reader;
    CHECK(grantor.GrantTo(1, writer, Right::Send).Ok());
    CHECK(grantor.GrantTo(1, reader, Right::Receive).Ok());
    CHECK(grantor.Close(1).Ok());
    CHECK(strong->ReceiverCount() == 1);
    reader.Retire();
    CHECK(writer.Status(1, Right::Send).error == Error::PeerClosed);
    CHECK(strong->SenderCount() == 1);
    writer.Retire();
    CHECK(strong->SenderCount() == 0);
    CHECK(strong->ReceiverCount() == 0);

    // Ordinary HandleTable destruction also relinquishes active endpoints.
    std::shared_ptr<MessageQueue> retained;
    {
        HandleTable scoped;
        CHECK(scoped.CreateQueue(1).Ok());
        retained = scoped.Get(1, Right::Send);
        CHECK(retained->SenderCount() == 1 && retained->ReceiverCount() == 1);
    }
    CHECK(retained->SenderCount() == 0 && retained->ReceiverCount() == 0);
    CHECK(retained->Readiness(Right::Send).error == Error::PeerClosed);
    CHECK(retained->Readiness(Right::Receive).error == Error::PeerClosed);

    // A full queue is not writable until a consumer drains one entry.
    HandleTable saturation;
    CHECK(saturation.CreateQueue(1).Ok());
    auto full_queue = saturation.Get(1, Right::Send);
    CHECK(full_queue->Send(packet).Ok());
    CHECK(saturation.Status(1, Right::Send).error == Error::Full);
    CHECK(saturation.Status(1, Right::Receive).Ok());
    CHECK(full_queue->Consume().Ok());
    CHECK(saturation.Status(1, Right::Send).Ok());

    std::cout << "Capability endpoints, peer closure, retirement and readiness passed\n";
    return EXIT_SUCCESS;
}
