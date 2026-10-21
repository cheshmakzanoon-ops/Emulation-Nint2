// An original, synthetic-only ARM64 receiver with nonblocking retry.
.text
.global _start
_start:
    mov x8, #6
    mov x0, #1          // Host-granted Receive-only handle
    mov x1, #0x3000     // Receiver-private writable page
    mov x2, #5
retry:
    svc #0             // QueueReceive, returns -11 if empty
    adds xzr, x0, #11
    b.ne received
    mov x0, #1
    b retry
received:
    mov x2, x0         // Received byte count
    mov x8, #1         // Write to captured stdout
    mov x0, #1
    svc #0
    mov x8, #2         // Exit(0)
    mov x0, #0
    svc #0
