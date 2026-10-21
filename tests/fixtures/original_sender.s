// An original, synthetic-only ARM64 sender. Not console software.
.text
.global _start
_start:
    mov x8, #4          // CreateQueue(capacity=2)
    mov x0, #2
    svc #0
    mov x8, #5          // QueueSend(handle=x0, address, len=5)
    adr x1, message
    mov x2, #5
    svc #0
    mov x8, #7          // Close the producer handle; recipient keeps its own
    mov x0, #1
    svc #0
    mov x8, #2
    mov x0, #0
    svc #0
message:
    .ascii "PING!"
