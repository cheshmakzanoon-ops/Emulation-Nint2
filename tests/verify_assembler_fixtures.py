#!/usr/bin/env python3
"""Compare original AArch64 assembly with checked-in host CPU fixture words.

Offline test: only Clang AArch64 assembler, llvm-objcopy, and Python stdlib.
"""
from pathlib import Path
import argparse
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# ASM source, host regression source, array, number of assembled words to check.
# Some host regressions replace an ASM RET with a deliberately invalid opcode.
FIXTURES = [
    ("original_logical_ops.s", "cpu_logical_tests.cpp", "code", 13),
    ("original_sender.s", "scheduler_tests.cpp", "sender_code", 13),
    ("original_sender.s", "multi_guest_elf_tests.cpp", "kSender", 13),
    ("original_receiver.s", "scheduler_tests.cpp", "receiver_code", 16),
    ("original_receiver.s", "multi_guest_elf_tests.cpp", "kReceiver", 16),
    ("original_divide_shift.s", "cpu_divide_shift_tests.cpp", "instructions", 10),
    ("original_carry.s", "cpu_carry_tests.cpp", "code", 10),
    ("original_extract.s", "cpu_extract_tests.cpp", "words", 9),
    ("original_sp_logical.s", "cpu_sp_logical_tests.cpp", "words", 17),
    ("wait_sender.s", "queue_wait_tests.cpp", "kSender", 20),
    ("wait_receiver.s", "queue_wait_tests.cpp", "kReceiver", 25),
    ("original_asset_guest.s", "asset_guest_tests.cpp", "kWords", 34),
    ("original_vm_guest.s", "vm_guest_tests.cpp", "kWords", 46),
    ("original_heap_asset.s", "heap_guest_tests.cpp", "kWords", 46),
    ("original_atom.s", "cpu_atomic_tests.cpp", "insns", 21),
]

def host_words(path: Path, name: str, count: int) -> list[int]:
    source = path.read_text(encoding="utf-8")
    expression = (r"constexpr\s+std::array\s*<\s*std::uint32_t\s*,\s*\d+\s*>\s+"
                  + re.escape(name) + r"\s*=?\s*\{\{?(.*?)\}\}?\s*;")
    match = re.search(expression, source, re.DOTALL)
    if not match:
        raise ValueError(f"missing {name} array in {path}")
    raw = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    words = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", raw)]
    if len(words) < count:
        raise ValueError(f"insufficient {name} words: {len(words)} < {count}")
    return words[:count]

def assembled_words(source: Path, temporary: Path, count: int,
                    clang: str, objcopy: str) -> list[int]:
    obj = temporary.with_suffix(".o")
    output = temporary.with_suffix(".bin")
    subprocess.run([clang, "--target=aarch64-none-elf", "-c", str(source),
                    "-o", str(obj)], check=True)
    subprocess.run([objcopy, "-O", "binary", "--only-section=.text",
                    str(obj), str(output)], check=True)
    data = output.read_bytes()
    if len(data) < 4 * count:
        raise ValueError(f"truncated assembled fixture {source}: {len(data)} bytes")
    return list(struct.unpack("<" + "I" * count, data[:4 * count]))

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    args = parser.parse_args()
    failed = False
    with tempfile.TemporaryDirectory(prefix="nint2-original-asm-") as folder:
        cache: dict[tuple[str,int],list[int]] = {}
        for fixture, host, array, count in FIXTURES:
            key = (fixture, count)
            if key not in cache:
                cache[key] = assembled_words(ROOT / "tests/fixtures" / fixture,
                                             Path(folder) / Path(fixture).stem, count,
                                             args.clang, args.objcopy)
            actual = cache[key]
            expected = host_words(ROOT / "tests" / host, array, count)
            if actual != expected:
                failed = True
                for i, (assembled, embedded) in enumerate(zip(actual, expected)):
                    if assembled != embedded:
                        print(f"FAIL {fixture} {host}:{array}[{i}]: "
                              f"assembled={assembled:08x}, embedded={embedded:08x}",
                              file=sys.stderr)
            else:
                print(f"PASS {fixture} -> {host}:{array} ({count} A64 words)")
    return 1 if failed else 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ARM64 fixture verifier failed: {error}", file=sys.stderr)
        sys.exit(1)
