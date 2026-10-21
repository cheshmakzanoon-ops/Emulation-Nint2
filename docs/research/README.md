# Research cards

Create one Markdown file per falsifiable hardware/software behavior hypothesis. Use a stable prefix such as CPU-ISA-001, GPU-FIFO-001, KERNEL-SVC-001, IPC-001, HID-001. Follow [HARDWARE_RESEARCH.md](../HARDWARE_RESEARCH.md).

## Card template

~~~text
Title:
ID:
Hypothesis / competing alternatives:
Subsystem / priority:
Confidence: CONFIRMED | REPORTED | INFERRED | UNKNOWN | BLOCKED
Sources and revisions:
Device/OS revisions when genuinely known:
Legal provenance and redistribution rights:
Original fixture source and expected outcome:
Procedure, controls and repetition count:
Observed outcomes, uncertainty and failure cases:
Falsifying observation:
Implementation/test/commit references:
Revalidation trigger:
~~~

Research notes are not equivalent to emulator support until the implementation, independent fixture and regression test are present. Never include proprietary binaries, keys, unlicensed protected traces or confidential SDK material.
