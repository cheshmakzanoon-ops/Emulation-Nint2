#!/usr/bin/env python3
"""Independently compile original ARM64 sources and run the complete runtime.

Requires an existing native build and optional Clang/LLD/LLVM tools. Uses only
Python's standard library. No downloads, firmware, libraries or console data.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

import regenerate_rpc_fixtures as rpc

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures"


def run(command, expected=None):
    result = subprocess.run([str(part) for part in command], check=True,
                            capture_output=True, text=True, timeout=60)
    if expected is not None and result.stdout != expected:
        raise RuntimeError(f"Output mismatch for {command[0]}: {result.stdout!r}\n{result.stderr}")
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--lld", default="ld.lld")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--check-rpc-fixtures", action="store_true",
                        help="also require the recorded Clang/LLD 18 fixture bytes")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / "independent-guests"
    output.mkdir(parents=True, exist_ok=True)
    linker = shutil.which(args.lld)
    if not linker:
        raise RuntimeError(f"LLD linker not found: {args.lld}")
    extension = ".exe" if (build / "nint2.exe").exists() else ""
    runtime = build / ("nint2" + extension)
    for tool in (args.clang, linker, args.objdump):
        version = subprocess.run([tool, "--version"], check=True, capture_output=True,
                                 text=True, timeout=30).stdout.splitlines()[0]
        print(version)

    flags = [args.clang, "--target=aarch64-none-elf", "-nostdlib",
             f"-fuse-ld={linker}", "-Wl,-e,_start",
             "-Wl,-Ttext=0x10000", "-Wl,--build-id=none",
             "-Wl,-z,max-page-size=4096"]
    cases = [
        ("original_compiled_guest", "-O0", "Compiled C on AArch64!\n"),
        ("original_compiled_ipc", "-O0", "C guest IPC verified!\n"),
        ("original_division_c", "-O1", "AArch64 C divide verified!\n"),
        ("original_ror_c", "-O1", "Optimized ARM64 rotate!\n"),
    ]
    for name, optimization, expected in cases:
        elf = output / f"{name}.elf"
        run(flags + [optimization, "-ffreestanding", "-fno-builtin",
                     "-fno-stack-protector", "-fno-unwind-tables",
                     FIXTURES / f"{name}.c", "-o", elf])
        run([runtime, "--run-synthetic", elf], expected)
        if name == "original_ror_c":
            disassembly = subprocess.run([args.objdump, "-d", str(elf)], check=True,
                                         capture_output=True, text=True, timeout=30).stdout
            if not re.search(r"\bror\s+x\d+,\s*x\d+,\s*#(?:0x11|17)\b", disassembly):
                raise RuntimeError("original rotate fixture no longer emits ROR #17")

    for role in ("sender", "receiver"):
        run(flags + [FIXTURES / f"original_{role}.s", "-o", output / f"{role}.elf"])
    run([build / ("nint2_multi_guest_elf_tests" + extension), "--run-pair",
         output / "sender.elf", output / "receiver.elf"])

    generated = rpc.generate(output, args.clang, linker)
    if args.check_rpc_fixtures and generated != rpc.HEADER.read_text(encoding="utf-8"):
        raise RuntimeError("original RPC PT_LOAD bytes differ from the recorded Clang/LLD 18 image")
    client, server = output / "original_rpc_client.elf", output / "original_rpc_server.elf"
    run([build / ("nint2_compiled_rpc_tests" + extension), "--run-pair", client, server])
    run([runtime, "--run-synthetic-pair", client, server],
        "C RPC client: 64 replies verified!\nC RPC server: 64 requests processed!\n")
    print("PASS: four original C guests, original assembly pair and compiled C RPC pair")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Guest tool/run failed: {error.cmd}\n{error.stdout or ''}{error.stderr or ''}")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Guest verification failed: {error}")
