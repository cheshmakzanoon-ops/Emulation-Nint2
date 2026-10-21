#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "nint2/arena.hpp"
#include "nint2/assets.hpp"
#include "nint2/cpu.hpp"
#include <span>
#include "nint2/kernel.hpp"

namespace nint2 {

// NOT a Nintendo/Horizon/Linux ABI. This original protocol is exclusively for
// source-controlled guest fixtures, with no passthrough to host system calls.
enum class SessionStop : std::uint8_t {
    Exited,
    CpuException,
    UnsupportedSupervisorCall,
    StepLimit,
    HostResourceFailure,
    Blocked // voluntary synthetic QueueWait, not an architectural CPU trap
};
struct SessionResult {
    SessionStop stop{SessionStop::StepLimit};
    CpuStop cpu_stop{CpuStop::Running};
    std::uint64_t steps{};          // cumulative executed guest instructions
    std::uint64_t exit_code{};
    std::uint64_t fault_address{};
    std::uint64_t service_number{};
};

class SyntheticSession final {
public:
    static constexpr std::uint64_t kMaxWrite = 4096;
    static constexpr std::uint64_t kMaxOutput = 65536;

    SyntheticSession(GuestMemory& memory, std::uint64_t entry, std::uint64_t stack_top) noexcept;
    [[nodiscard]] SessionResult Run(std::uint64_t max_steps);
    [[nodiscard]] CpuContext& Context() noexcept { return cpu_.context; }
    [[nodiscard]] const CpuContext& Context() const noexcept { return cpu_.context; }
    [[nodiscard]] std::string_view Stdout() const noexcept { return stdout_; }
    [[nodiscard]] std::string_view Stderr() const noexcept { return stderr_; }
    [[nodiscard]] std::size_t HandleCount() const noexcept { return handles_.Size(); }
    [[nodiscard]] std::uint64_t TotalSteps() const noexcept { return steps_; }
    // A blocked synthetic SVC is resumable only after state changes. These
    // methods are for the host-owned cooperative scheduler, not guest ABI.
    [[nodiscard]] bool CanResume() const noexcept;
    void RetireHandles() noexcept {
        handles_.Retire();
        assets_.CloseAll();
        arena_.ReleaseAll();
    }
    // Host supplies immutable original data before the first Run(); the guest
    // never opens an arbitrary host file or modifies mounted bytes.
    [[nodiscard]] assets::Result MountAsset(std::string_view path,
        std::span<const std::uint8_t> bytes) { return assets_.Mount(path, bytes); }
    [[nodiscard]] std::size_t OpenAssetCount() const noexcept { return assets_.OpenCount(); }
    [[nodiscard]] std::size_t AssetCount() const noexcept { return assets_.AssetCount(); }
    [[nodiscard]] std::size_t ArenaRegionCount() const noexcept { return arena_.RegionCount(); }
    [[nodiscard]] std::uint64_t ArenaAllocatedBytes() const noexcept { return arena_.AllocatedBytes(); }
    // Host-owned bootstrap authority; imports an attenuated capability before
    // guest execution without depending on compiler-generated startup steps.
    // This does not introduce a guest-visible service or share address spaces.
    [[nodiscard]] kernel::Result ImportQueue(const kernel::HandleTable& owner,
                                             std::uint32_t handle,
                                             kernel::Right rights);
    // Host-authorized test harness grant only, not a guest-visible SVC.
    // Guest sessions retain distinct memory/register/handle namespaces.
    [[nodiscard]] kernel::Result GrantQueueTo(std::uint32_t handle,
                                              SyntheticSession& recipient,
                                              kernel::Right rights) const;

private:
    [[nodiscard]] std::uint64_t HandleWrite();
    [[nodiscard]] std::uint64_t HandleQueueCreate();
    [[nodiscard]] std::uint64_t HandleQueueSend();
    [[nodiscard]] std::uint64_t HandleQueueReceive();
    [[nodiscard]] std::uint64_t HandleQueueClose();
    [[nodiscard]] std::uint64_t HandleQueueDuplicate();
    [[nodiscard]] std::uint64_t HandleAssetOpen();
    [[nodiscard]] std::uint64_t HandleAssetRead(bool positioned);
    [[nodiscard]] std::uint64_t HandleAssetSeek();
    [[nodiscard]] std::uint64_t HandleAssetClose();
    [[nodiscard]] std::uint64_t HandleAssetStat();
    [[nodiscard]] std::uint64_t HandleAssetEnumerate();
    [[nodiscard]] std::uint64_t HandleAssetFstat();
    [[nodiscard]] std::uint64_t HandleGuestAllocate();
    [[nodiscard]] std::uint64_t HandleGuestFree();
    [[nodiscard]] std::uint64_t HandleGuestProtect();
    [[nodiscard]] std::uint64_t HandleGuestQuery();
    [[nodiscard]] SessionResult Outcome(SessionStop stop, CpuStop cpu_stop,
                                        std::uint64_t fault = 0,
                                        std::uint64_t service = 0) const noexcept;
    GuestMemory& memory_;
    CpuInterpreter cpu_;
    kernel::HandleTable handles_;
    assets::AssetStore assets_;
    vm::GuestArena arena_{memory_};
    std::string stdout_;
    std::string stderr_;
    std::uint64_t steps_{};
    std::uint64_t exit_code_{};
    bool exited_{};
    bool waiting_{};
    std::uint32_t wait_handle_{};
    kernel::Right wait_direction_{kernel::Right::None};
    std::size_t wait_bytes_{}; // x3 requested size for blocked Send readiness
};
} // namespace nint2
