#!/usr/bin/env python3
"""Compile only our original C guests and emit compact offline ELF load images.

Python stdlib + Clang/LLD. Nothing is downloaded. Default --output-dir is a
temporary directory; pass --output-dir build/rpc to keep independently linked
ELFs for --run-pair or nint2 --run-synthetic-pair. --check verifies the recorded
Clang 18 fixture bytes without changing the checked-in generated header.
"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "tests/fixtures/compiled_rpc_images.hpp"


def read_image(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("expected ELF64 little endian")
    if struct.unpack_from("<HH", data, 16) != (2, 183):
        raise ValueError("expected static AArch64 ELF")
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    segments = []
    for i in range(phcount):
        kind, flags, offset, va, _, size, memory_size, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phsize)
        if kind != 1:
            continue
        raw = data[offset:offset + size]
        if len(raw) != size:
            raise ValueError("truncated original ELF")
        # Segment words retain exact PT_LOAD bytes, including ELF headers when
        # the linker emits a header segment. Padding is excluded by file_size.
        padded = raw + b"\0" * (-len(raw) % 4)
        words = struct.unpack("<" + "I" * (len(padded) // 4), padded)
        segments.append((va, memory_size, flags, size, words))
    return entry, segments


def generate(directory, clang, lld):
    lines = [
        "// Generated from original_rpc_client.c / original_rpc_server.c only.",
        "// Clang 18, -O1 -mgeneral-regs-only; regenerate_rpc_fixtures.py records",
        "// exact PT_LOAD bytes. No console software or third-party guest code.",
        "#pragma once", "#include <array>", "#include <cstddef>",
        "#include <cstdint>", "#include <span>", "namespace original_rpc {",
        "struct Segment {", "    std::uint64_t address, memory_size;",
        "    unsigned flags;", "    std::size_t file_size;",
        "    std::span<const std::uint32_t> words;", "};",
        "struct Image { std::uint64_t entry; std::span<const Segment> segments; };",
    ]
    for role in ("client", "server"):
        output = directory / f"original_rpc_{role}.elf"
        subprocess.run([
            clang, "--target=aarch64-none-elf", "-O1", "-mgeneral-regs-only",
            "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
            "-fno-unwind-tables", "-nostdlib", f"-fuse-ld={lld}",
            "-Wl,-e,_start", "-Wl,-Ttext=0x10000", "-Wl,--build-id=none",
            "-Wl,-z,max-page-size=4096",
            str(ROOT / "tests/fixtures" / f"original_rpc_{role}.c"),
            "-o", str(output),
        ], check=True)
        entry, segments = read_image(output)
        prefix = role.capitalize()
        for i, (_, _, _, _, words) in enumerate(segments):
            lines.append(f"inline constexpr std::array<std::uint32_t, {len(words)}> k{prefix}Words{i}{{{{")
            for offset in range(0, len(words), 6):
                lines.append("    " + ", ".join(f"0x{w:08x}" for w in words[offset:offset + 6]) + ",")
            lines.append("}};")
        lines.append(f"inline constexpr std::array<Segment, {len(segments)}> k{prefix}Segments{{{{")
        for i, (va, memory, flags, size, _) in enumerate(segments):
            lines.append(f"    {{0x{va:x}, 0x{memory:x}, {flags}, {size}, k{prefix}Words{i}}},")
        lines.append("}};")
        lines.append(f"inline constexpr Image k{prefix}{{0x{entry:x}, k{prefix}Segments}};")
    lines.append("} // namespace original_rpc")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--lld", default="ld.lld")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--compile-only", action="store_true",
                        help="keep linked ELF guests without updating/checking recorded bytes")
    args = parser.parse_args()
    linker = shutil.which(args.lld)
    if not linker:
        raise SystemExit(f"LLD linker not found: {args.lld}")
    with tempfile.TemporaryDirectory(prefix="nint2-rpc-") as temp:
        directory = args.output_dir or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        generated = generate(directory, args.clang, linker)
    if args.compile_only:
        return 0
    if args.check:
        if HEADER.read_text(encoding="utf-8") != generated:
            raise SystemExit("Original RPC fixture bytes differ: use their recorded Clang 18 toolchain")
        print("Original C RPC fixture PT_LOAD bytes verified")
    else:
        HEADER.write_text(generated, encoding="utf-8")
        print(f"Wrote {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
