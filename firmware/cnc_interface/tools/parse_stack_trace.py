#!/usr/bin/env python3
"""
Quick helper to parse an ESP32 RISC-V crash log and resolve addresses
to source lines using riscv32-esp-elf-addr2line.

Usage:
  python3 tools/parse_stack_trace.py -e <elf> [--addr2line PATH] <crash-log-file>
  cat crash.txt | python3 tools/parse_stack_trace.py -e .pio/build/.../firmware.elf

The script extracts hex addresses from register lines and stack memory
and runs addr2line -f -p to print function+file:line mappings.
"""

import re
import sys
import argparse
import subprocess
import shutil
from pathlib import Path

REG_RE = re.compile(r"^\s*(MEPC|RA|SP|GP|TP|T[0-6]|S[0-9]|A[0-7]|MCAUSE|MTVAL)\s*:\s*(0x[0-9a-fA-F]+)")
HEX_RE = re.compile(r"0x[0-9a-fA-F]{6,}")


def find_addr2line(candidate=None):
    if candidate:
        p = Path(candidate).expanduser()
        if p.exists():
            return str(p)
    # Common PlatformIO location
    pio_tool = Path.home() / ".platformio" / "packages" / "toolchain-riscv32-esp" / "bin" / "riscv32-esp-elf-addr2line"
    if pio_tool.exists():
        return str(pio_tool)
    # Fallback to PATH
    return "riscv32-esp-elf-addr2line"


def extract_addresses(text):
    addrs = []
    seen = set()
    # registers first (preserve order)
    for ln in text.splitlines():
        m = REG_RE.match(ln)
        if m:
            a = m.group(2)
            if a not in seen:
                seen.add(a); addrs.append(a)
    # then scan stack hex columns
    for m in HEX_RE.finditer(text):
        a = m.group(0)
        if a not in seen:
            seen.add(a); addrs.append(a)
    return addrs


def read_clipboard():
    """Try to read clipboard using common tools (pbpaste, xclip, xsel).
    Returns tuple (text, used_cmd) or (None, None) if none available.
    """
    candidates = [
        (['pbpaste'], 'pbpaste'),
        (['xclip', '-selection', 'clipboard', '-o'], 'xclip'),
        (['xsel', '--clipboard', '--output'], 'xsel'),
    ]
    for cmd, name in candidates:
        if shutil.which(cmd[0]):
            try:
                p = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            except FileNotFoundError:
                continue
            if p.returncode == 0:
                return p.stdout, name
    return None, None


def run_addr2line(addr2line, elf, addrs):
    if not addrs:
        print("No addresses found in input.")
        return 1
    cmd = [addr2line, '-e', elf, '-f', '-p'] + addrs
    try:
        p = subprocess.run(cmd, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    except FileNotFoundError:
        print(f"addr2line binary not found: {addr2line}")
        return 2
    if p.stderr:
        print("[addr2line stderr]", p.stderr, file=sys.stderr)
    print(p.stdout)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-e', '--elf', required=True, help='Path to firmware ELF')
    ap.add_argument('--addr2line', help='Path to riscv addr2line binary')
    ap.add_argument('--clipboard', action='store_true', help='Read crash log from system clipboard when file is omitted')
    ap.add_argument('file', nargs='?', help='Crash log file (defaults to stdin)')
    args = ap.parse_args()

    if args.file:
        txt = Path(args.file).read_text()
    elif args.clipboard:
        txt, used = read_clipboard()
        if txt is None:
            print('Clipboard paste command not found (tried pbpaste/xclip/xsel).', file=sys.stderr)
            return 3
        print(f'Input read from clipboard via {used}.', file=sys.stderr)
    else:
        txt = sys.stdin.read()

    addrs = extract_addresses(txt)
    if not addrs:
        print('No addresses detected in crash log.', file=sys.stderr)
        return 1

    print('Resolved addresses (in order):')
    print(' '.join(addrs))
    elf_path = Path(args.elf)
    if not elf_path.exists():
        print(f'ELF not found: {args.elf}', file=sys.stderr)
        return 2

    addr2line = find_addr2line(args.addr2line)
    return run_addr2line(addr2line, str(elf_path), addrs)


if __name__ == '__main__':
    sys.exit(main())
