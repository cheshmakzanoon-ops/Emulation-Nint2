// Original C client. Cross-compile with Clang/LLD using regenerate_rpc_fixtures.py.
#include "original_rpc.h"

void _start(long send, long receive, volatile struct RpcReport* report) {
    struct RpcPacket packet;
    uint64_t digest = 0;
    rpc_initialize(report);
    rpc_check_rights(send, receive, &packet, report);
    // Two requests before two replies deliberately exercise capacity-one queue
    // backpressure. The server computes every reply inside the ARM64 interpreter.
    for (uint64_t batch = 1; batch <= RPC_ROUNDS; batch += 2) {
        for (uint64_t offset = 0; offset != 2; ++offset) {
            const uint64_t sequence = batch + offset;
            const uint64_t value = rpc_request_value(sequence);
            packet.sequence = sequence;
            packet.value = value;
            packet.inverse = ~value;
            packet.checksum = rpc_checksum(value, sequence);
            if (report && report->corrupt_sequence == sequence) packet.checksum ^= 1;
            rpc_send(send, &packet, report);
        }
        for (uint64_t offset = 0; offset != 2; ++offset) {
            const uint64_t sequence = batch + offset;
            rpc_receive(receive, &packet, report);
            if (packet.sequence != sequence) rpc_finish(report, 101);
            const uint64_t expected = rpc_reply_value(rpc_request_value(sequence), sequence);
            if (packet.value != expected) rpc_finish(report, 102);
            if (packet.inverse != ~packet.value) rpc_finish(report, 103);
            if (packet.checksum != rpc_checksum(packet.value, sequence)) rpc_finish(report, 104);
            digest = rpc_digest(digest, packet.value, sequence);
            if (report) {
                report->completed = sequence;
                report->digest = digest;
            }
        }
    }
    rpc_close(send, receive, report);
    static const char message[] = "C RPC client: 64 replies verified!\n";
    if (rpc_service(1, 1, (long)message, sizeof(message) - 1) != sizeof(message) - 1)
        rpc_finish(report, 105);
    rpc_finish(report, report ? report->final_exit_code : 0);
}
