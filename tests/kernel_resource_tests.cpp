#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include "nint2/kernel.hpp"

// This executable alone replaces allocation to exercise each allocation point
// of bounded kernel operations. The guard disables injection before assertions,
// diagnostics, cleanup, or any unrelated test code can allocate.
namespace allocation_failure {
bool enabled = false;
std::size_t remaining = 0;

class Guard final {
public:
    explicit Guard(std::size_t successes) noexcept {
        remaining = successes;
        enabled = true;
    }
    ~Guard() { enabled = false; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
} // namespace allocation_failure

void* operator new(std::size_t bytes) {
    if (allocation_failure::enabled) {
        if (allocation_failure::remaining == 0) throw std::bad_alloc();
        --allocation_failure::remaining;
    }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

#define EXPECT(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " #condition << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using namespace nint2::kernel;
    bool creation_succeeded = false;
    std::size_t creation_failures = 0;
    for (std::size_t position = 0; position < 32; ++position) {
        HandleTable table;
        Result created;
        {
            allocation_failure::Guard inject(position);
            created = table.CreateQueue(2);
        }
        if (created.Ok()) {
            EXPECT(created.value == 1 && table.Size() == 1);
            creation_succeeded = true;
            break;
        }
        ++creation_failures;
        EXPECT(created.error == Error::OutOfMemory);
        EXPECT(table.Size() == 0);
        // Allocation failure must neither leak a capability nor burn its ID.
        const auto recovered = table.CreateQueue(2);
        EXPECT(recovered.Ok() && recovered.value == 1 && table.Size() == 1);
    }
    EXPECT(creation_succeeded && creation_failures > 0);

    const std::array<std::uint8_t, 3> first{'O', 'N', 'E'};
    const std::array<std::uint8_t, 4> second{'N', 'E', 'X', 'T'};
    bool send_succeeded = false;
    std::size_t send_failures = 0;
    for (std::size_t position = 0; position < 32; ++position) {
        MessageQueue queue(2);
        EXPECT(queue.Send(first).Ok());
        Result sent;
        {
            allocation_failure::Guard inject(position);
            sent = queue.Send(second);
        }
        if (!sent.Ok()) {
            ++send_failures;
            EXPECT(sent.error == Error::OutOfMemory);
            EXPECT(queue.Depth() == 1 && queue.QueuedBytes() == first.size());
            EXPECT(queue.Send(second).Ok());
        } else {
            send_succeeded = true;
        }
        std::array<std::uint8_t, 4> bytes{};
        EXPECT(queue.Peek(bytes).value == first.size());
        for (std::size_t i = 0; i < first.size(); ++i) EXPECT(bytes[i] == first[i]);
        EXPECT(queue.Consume().value == first.size());
        EXPECT(queue.Peek(bytes).value == second.size() && bytes == second);
        EXPECT(queue.Consume().value == second.size());
        EXPECT(queue.Depth() == 0 && queue.QueuedBytes() == 0);
        if (send_succeeded) break;
    }
    EXPECT(send_succeeded && send_failures > 0);

    bool grant_succeeded = false;
    std::size_t grant_failures = 0;
    for (std::size_t position = 0; position < 32; ++position) {
        HandleTable owner;
        HandleTable recipient;
        const auto source = owner.CreateQueue(1);
        EXPECT(source.Ok() && source.value == 1);
        const auto queue = owner.Get(1, Right::Send);
        EXPECT(queue && queue->Send(first).Ok());
        Result granted;
        {
            allocation_failure::Guard inject(position);
            granted = owner.GrantTo(1, recipient, Right::Receive);
        }
        if (!granted.Ok()) {
            ++grant_failures;
            EXPECT(granted.error == Error::OutOfMemory);
            EXPECT(owner.Size() == 1 && recipient.Size() == 0);
            EXPECT(owner.Get(1, Right::Send) == queue && queue->Depth() == 1);
            granted = owner.GrantTo(1, recipient, Right::Receive);
            EXPECT(granted.Ok());
        } else {
            grant_succeeded = true;
        }
        EXPECT(granted.value == 1 && recipient.Size() == 1);
        EXPECT(recipient.Get(1, Right::Receive) == queue);
        EXPECT(recipient.Check(1, Right::Send) == Error::PermissionDenied);
        if (grant_succeeded) break;
    }
    EXPECT(grant_succeeded && grant_failures > 0);
    std::cout << "Kernel allocation failures preserve queues, capabilities and handle IDs ("
              << creation_failures << " create, " << send_failures << " send, "
              << grant_failures << " grant allocation points)\n";
    return EXIT_SUCCESS;
}
