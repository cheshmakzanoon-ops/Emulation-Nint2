#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "nint2/kernel.hpp"
#define EXPECT(test) do { if (!(test)) { std::cerr << "FAIL: " #test << " line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

int main() {
    using namespace nint2::kernel;
    HandleTable table(3);
    EXPECT(table.CreateQueue(0).error == Error::InvalidArgument);
    EXPECT(table.CreateQueue(65).error == Error::InvalidArgument);
    auto original = table.CreateQueue(2);
    EXPECT(original.Ok() && original.value == 1);
    const auto original_handle = static_cast<std::uint32_t>(original.value);
    auto send_only = table.Duplicate(original_handle, Right::Send);
    auto receive_only = table.Duplicate(original_handle, Right::Receive);
    EXPECT(send_only.Ok() && receive_only.Ok());
    EXPECT(table.Size() == 3);
    EXPECT(table.CreateQueue(1).error == Error::Exhausted);
    EXPECT(table.Check(static_cast<std::uint32_t>(send_only.value), Right::Receive) == Error::PermissionDenied);
    EXPECT(!table.Get(static_cast<std::uint32_t>(receive_only.value), Right::Send));
    EXPECT(table.Duplicate(static_cast<std::uint32_t>(send_only.value), Right::Send).error == Error::PermissionDenied);
    EXPECT(table.Duplicate(original_handle, Right::None).error == Error::PermissionDenied);
    EXPECT(table.Duplicate(original_handle, static_cast<Right>(0x80)).error == Error::PermissionDenied);
    auto queue = table.Get(static_cast<std::uint32_t>(send_only.value), Right::Send);
    EXPECT(queue && queue->Capacity() == 2);
    const std::array<std::uint8_t, 3> abc{'A', 'B', 'C'};
    EXPECT(queue->Send(abc).Ok());
    EXPECT(queue->Send(std::span<const std::uint8_t>{}).Ok());
    EXPECT(queue->Depth() == 2 && queue->QueuedBytes() == 3);
    EXPECT(queue->Send(abc).error == Error::Full);
    auto reader = table.Get(static_cast<std::uint32_t>(receive_only.value), Right::Receive);
    EXPECT(reader == queue);
    std::array<std::uint8_t, 2> short_buffer{};
    EXPECT(reader->Peek(short_buffer).error == Error::BufferTooSmall);
    EXPECT(reader->Depth() == 2);
    std::array<std::uint8_t, 3> out{};
    EXPECT(reader->Peek(out).value == 3 && out == abc);
    EXPECT(reader->Consume().value == 3);
    EXPECT(reader->Peek(out).Ok());
    EXPECT(reader->Consume().Ok() && reader->Peek(out).error == Error::Empty);
    EXPECT(reader->Consume().error == Error::Empty);
    EXPECT(table.Close(original_handle).Ok());
    EXPECT(table.Close(original_handle).error == Error::InvalidHandle);
    EXPECT(table.Check(original_handle, Right::Send) == Error::InvalidHandle);
    EXPECT(table.Get(static_cast<std::uint32_t>(send_only.value), Right::Send) == queue);
    auto later = table.CreateQueue(1);
    EXPECT(later.Ok() && later.value > receive_only.value);
    EXPECT(table.Check(0, Right::Send) == Error::InvalidHandle);
    EXPECT(!table.Duplicate(0, Right::Send).Ok());

    MessageQueue limited(64);
    std::array<std::uint8_t, MessageQueue::kMaxMessage + 1> oversized{};
    EXPECT(limited.Send(oversized).error == Error::InvalidArgument);
    const std::array<std::uint8_t, MessageQueue::kMaxMessage> full_message{};
    for (unsigned i = 0; i < 16; ++i) EXPECT(limited.Send(full_message).Ok());
    EXPECT(limited.QueuedBytes() == MessageQueue::kMaxQueuedBytes);
    EXPECT(limited.Send(abc).error == Error::Full);
    EXPECT(limited.Consume().value == 4096);
    EXPECT(limited.Send(abc).Ok());
    EXPECT(limited.QueuedBytes() == 15 * 4096 + 3);
    // Original host-only cross-session capability grants: separate handle IDs,
    // shared queue storage, no authority amplification and no stale-handle reuse.
    HandleTable producer(3);
    HandleTable consumer(1);
    const auto source = producer.CreateQueue(2);
    EXPECT(source.Ok() && source.value == 1);
    const auto granted = producer.GrantTo(static_cast<std::uint32_t>(source.value),
                                          consumer, Right::Receive);
    EXPECT(granted.Ok() && granted.value == 1);
    EXPECT(consumer.Size() == 1 && producer.Size() == 1);
    EXPECT(consumer.Check(1, Right::Send) == Error::PermissionDenied);
    EXPECT(consumer.Check(1, Right::Duplicate) == Error::PermissionDenied);
    EXPECT(consumer.GrantTo(1, producer, Right::Receive).error == Error::PermissionDenied);
    EXPECT(producer.GrantTo(1, consumer, Right::Send).error == Error::Exhausted);
    EXPECT(producer.GrantTo(1, consumer, Right::None).error == Error::PermissionDenied);
    EXPECT(producer.GrantTo(1, consumer, static_cast<Right>(0x80)).error == Error::PermissionDenied);
    const auto source_queue = producer.Get(1, Right::Send);
    const auto recipient_queue = consumer.Get(1, Right::Receive);
    EXPECT(source_queue && source_queue == recipient_queue);
    EXPECT(source_queue->Send(abc).Ok());
    EXPECT(producer.Close(1).Ok());
    EXPECT(producer.GrantTo(1, consumer, Right::Receive).error == Error::InvalidHandle);
    EXPECT(recipient_queue->Depth() == 1);
    std::array<std::uint8_t, 3> cross_process{};
    EXPECT(recipient_queue->Peek(cross_process).Ok() && cross_process == abc);
    EXPECT(recipient_queue->Consume().Ok() && recipient_queue->Depth() == 0);
    EXPECT(consumer.Close(1).Ok());
    EXPECT(consumer.Get(1, Right::Receive) == nullptr);
    const auto next = consumer.CreateQueue(1);
    EXPECT(next.Ok() && next.value > granted.value);

    std::cout << "Kernel handle table, rights, cross-session grants and IPC tests passed\n";
    return EXIT_SUCCESS;
}
