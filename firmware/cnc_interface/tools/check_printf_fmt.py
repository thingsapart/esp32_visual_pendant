#!/usr/bin/env python3
"""Pre-build linter: warn about %z printf format specifiers.

The nano-printf implementation (CONFIG_NEWLIB_NANO_FORMAT=y) used by ESP32
Arduino builds does NOT support the 'z' length modifier (%zu, %zd, %zi …).
Using these specifiers produces undefined behaviour at runtime even though
the build succeeds without a compiler warning.

Fix: replace every %zu/%zd/etc. with %u/%d and cast size_t arguments, or
disable nano-printf with CONFIG_NEWLIB_NANO_FORMAT=n in sdkconfig.defaults
(costs ~20 KB of flash).
"""

import os
import re
import sys

try:
    Import("env")  # PlatformIO injection
    _proj_dir = env.get("PROJECT_DIR", "")
except Exception:
    # Allow standalone execution for testing
    _proj_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

_src_dir = os.path.join(_proj_dir, "src")

_EXTENSIONS = {".c", ".cpp", ".h", ".hpp"}

# Matches %z followed by any conversion character.
# Handles optional flags/width/precision before the 'z'.
_PAT = re.compile(r'%[-+ #0]*\*?(?:\d+)?(?:\.\*?(?:\d+)?)?z[diouxXeEfgGaAnp]')


def _scan(src_dir):
    issues = []
    for root, dirs, files in os.walk(src_dir):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for fname in sorted(files):
            if os.path.splitext(fname)[1] not in _EXTENSIONS:
                continue
            path = os.path.join(root, fname)
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as f:
                    for lineno, line in enumerate(f, 1):
                        for m in _PAT.finditer(line):
                            issues.append((path, lineno, m.group()))
            except OSError:
                pass
    return issues


issues = _scan(_src_dir)

if issues:
    print()
    RED = "\x1b[31m"
    YELLOW = "\x1b[33m"
    RESET = "\x1b[0m"
    print("=" * 70)
    # Red, uppercase warning header
    print(f"{RED}[CHECK_PRINTF_FMT] WARNING: %z format specifiers detected.{RESET}")
    print("  Nano-printf (CONFIG_NEWLIB_NANO_FORMAT=y) does not support the")
    print("  'z' length modifier and will CRASH at runtime.")
    print("  Fix: replace %zu/%zd/etc. with %u/%d and cast size_t args,")
    print("  or set CONFIG_NEWLIB_NANO_FORMAT=n in sdkconfig.defaults.")
    print("-" * 70)
    for path, lineno, spec in issues:
        rel = os.path.relpath(path, _proj_dir)
        # Print file:line in yellow for visibility
        print(f"  {YELLOW}{rel}:{lineno}:{RESET} {spec!r}")
    print("=" * 70)
    print()
