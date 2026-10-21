// Original freestanding C fixtures for nint2's synthetic test ABI, not Horizon.
// Startup: x0 = send-only queue, x1 = receive-only queue, x2 = optional private
// 64-byte report buffer (zero disables reporting and fault injection).
#ifndef NINT2_ORIGINAL_RPC_H
#define NINT2_ORIGINAL_RPC_H
#include <stdint.h>

enum { RPC_ROUNDS = 64, RPC_PACKET_BYTES = 32 };
struct RpcPacket {
    uint64_t sequence;
    uint64_t value;
    uint64_t inverse;
    uint64_t checksum;
};
// Each field is uint64_t, at offsets 0,8,...,56. The guest clears the first six
// fields at entry. The final two are explicit, optional host test controls:
// corrupt one request's checksum; or exit nonzero after printing success.
struct RpcReport {
    uint64_t completed;
    uint64_t digest;
    uint64_t send_retries;
    uint64_t receive_retries;
    uint64_t rights_denials;
    uint64_t exit_code;
    uint64_t corrupt_sequence;
    uint64_t final_exit_code;
};

static long rpc_service(long call, long a, long b, long c) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = call;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

__attribute__((noreturn)) static void rpc_finish(volatile struct RpcReport* report,
                                               uint64_t code) {
    if (report) report->exit_code = code;
    rpc_service(2, (long)code, 0, 0);
    for (;;) {}
}

static void rpc_initialize(volatile struct RpcReport* report) {
    if (!report) return;
    report->completed = 0;
    report->digest = 0;
    report->send_retries = 0;
    report->receive_retries = 0;
    report->rights_denials = 0;
    report->exit_code = 0;
}

static uint64_t rpc_request_value(uint64_t sequence) {
    return (sequence * UINT64_C(0x100000001)) ^ UINT64_C(0x3141592653589793);
}
static uint64_t rpc_reply_value(uint64_t value, uint64_t sequence) {
    value ^= UINT64_C(0x1020304050607080);
    return ((value >> 17) | (value << 47)) + sequence * 33;
}
static uint64_t rpc_checksum(uint64_t value, uint64_t sequence) {
    return ((value >> 13) | (value << 51)) ^ sequence ^ UINT64_C(0xa5c369f00f963c5a);
}
static uint64_t rpc_digest(uint64_t previous, uint64_t value, uint64_t sequence) {
    previous ^= value;
    return ((previous >> 7) | (previous << 57)) + sequence;
}

static void rpc_send(long handle, struct RpcPacket* packet,
                     volatile struct RpcReport* report) {
    long result;
    do {
        result = rpc_service(5, handle, (long)packet, RPC_PACKET_BYTES);
        if (result == -11 && report) ++report->send_retries;
    } while (result == -11);
    if (result != RPC_PACKET_BYTES) rpc_finish(report, 91);
}
static void rpc_receive(long handle, struct RpcPacket* packet,
                        volatile struct RpcReport* report) {
    long result;
    do {
        result = rpc_service(6, handle, (long)packet, RPC_PACKET_BYTES);
        if (result == -11 && report) ++report->receive_retries;
    } while (result == -11);
    if (result != RPC_PACKET_BYTES) rpc_finish(report, 92);
}

static void rpc_check_rights(long send, long receive, struct RpcPacket* packet,
                            volatile struct RpcReport* report) {
    // Wrong-direction capabilities must fail before reading uninitialized data.
    if (rpc_service(5, receive, (long)packet, RPC_PACKET_BYTES) != -13)
        rpc_finish(report, 93);
    if (report) ++report->rights_denials;
    if (rpc_service(6, send, (long)packet, RPC_PACKET_BYTES) != -13)
        rpc_finish(report, 94);
    if (report) ++report->rights_denials;
}

static void rpc_close(long send, long receive, volatile struct RpcReport* report) {
    if (rpc_service(7, send, 0, 0) != 0) rpc_finish(report, 95);
    if (rpc_service(7, receive, 0, 0) != 0) rpc_finish(report, 96);
}
#endif
