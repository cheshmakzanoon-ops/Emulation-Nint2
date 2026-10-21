// Original C server, independently compiled/loaded into its own address space.
#include "original_rpc.h"

void _start(long send, long receive, volatile struct RpcReport* report) {
    struct RpcPacket packet;
    uint64_t digest = 0;
    rpc_initialize(report);
    rpc_check_rights(send, receive, &packet, report);
    for (uint64_t sequence = 1; sequence <= RPC_ROUNDS; ++sequence) {
        rpc_receive(receive, &packet, report);
        if (packet.sequence != sequence) rpc_finish(report, 111);
        if (packet.value != rpc_request_value(sequence)) rpc_finish(report, 112);
        if (packet.inverse != ~packet.value) rpc_finish(report, 113);
        if (packet.checksum != rpc_checksum(packet.value, sequence)) rpc_finish(report, 114);
        packet.value = rpc_reply_value(packet.value, sequence);
        packet.inverse = ~packet.value;
        packet.checksum = rpc_checksum(packet.value, sequence);
        rpc_send(send, &packet, report);
        digest = rpc_digest(digest, packet.value, sequence);
        if (report) {
            report->completed = sequence;
            report->digest = digest;
        }
    }
    rpc_close(send, receive, report);
    static const char message[] = "C RPC server: 64 requests processed!\n";
    if (rpc_service(1, 1, (long)message, sizeof(message) - 1) != sizeof(message) - 1)
        rpc_finish(report, 115);
    rpc_finish(report, report ? report->final_exit_code : 0);
}
