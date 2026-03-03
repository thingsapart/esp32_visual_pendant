#!/usr/bin/env python3
"""Analyze a GNU linker .map file and report internal-RAM consumers.

Usage:
    python tools/map_analyzer.py path/to/firmware.map [threshold_kb]
    python tools/map_analyzer.py path/to/firmware.map --ram-only
    python tools/map_analyzer.py path/to/firmware.map --ram-only --top 30

The script tracks which section each object-file contribution belongs to so
that --ram-only actually filters correctly.  Without --ram-only the raw
per-object totals across ALL sections are shown (flash code + RAM data
combined), which is misleading for RAM analysis.

Internal-RAM sections tracked (ESP32 / ESP32-S3):
  .dram0.data    -- initialised globals/statics  -> DRAM
  .dram0.bss     -- zero-initialised BSS          -> DRAM
  .iram0.text    -- functions with IRAM_ATTR      -> IRAM
  .iram0.vectors -- interrupt vectors             -> IRAM
  .noinit        -- no-init statics               -> DRAM
  .rtc.data / .rtc.bss -- RTC data RAM

Flash sections (.flash.text, .flash.rodata, .drom0.*) are excluded when
--ram-only is set.
"""

import sys
import re
import argparse
from collections import defaultdict


# Section prefixes that map to internal (on-chip) RAM on ESP32/ESP32-S3.
_RAM_PREFIXES = (
    ".dram0.data",
    ".dram0.bss",
    ".dram0.noinit",
    ".iram0.text",
    ".iram0.vectors",
    ".iram0",
    ".noinit",
    ".rtc.data",
    ".rtc.bss",
    ".rtc_noinit",
)

# Prefixes for flash / ROM sections.
_FLASH_PREFIXES = (
    ".flash.text",
    ".flash.appdesc",
    ".flash.rodata",
    ".drom0",
    ".rodata",
)


def _is_ram_section(name: str) -> bool:
    return any(name.startswith(p) for p in _RAM_PREFIXES)


def _is_flash_section(name: str) -> bool:
    return any(name.startswith(p) for p in _FLASH_PREFIXES)


def human_size(sz: int) -> str:
    if sz >= 1024 * 1024:
        return f"{sz / 1024 / 1024:.1f} MB"
    if sz >= 1024:
        return f"{sz / 1024:.1f} KB"
    return f"{sz} B"


def short_name(path: str) -> str:
    """Return a shorter, readable label for a library/object path."""
    # LTO temp objects: /var/folders/.../cchXXXX.ltransN.ltrans.o
    m = re.search(r"(cc\w+\.ltrans\d+\.ltrans\.o)$", path)
    if m:
        return f"<LTO: {m.group(1)}>"
    # Archive member:  /path/to/libfoo.a(bar.c.o)  ->  libfoo.a(bar.c.o)
    m = re.search(r"([^/\\]+\.a\([^)]+\))$", path)
    if m:
        return m.group(1)
    # Plain object:    /path/to/foo.c.o  ->  foo.c.o
    m = re.search(r"([^/\\]+\.o)$", path)
    if m:
        return m.group(1)
    return path


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

# Top-level section header (no leading whitespace):
#   ".dram0.bss  0x3fc9e000  0x4b20"
_SEC_HDR = re.compile(
    r"^(\.[A-Za-z0-9_.]+)\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)"
)

# Sub-section contribution inside a section block, contributed by an object:
#   " .dram0.bss  0x3fc9e100  0x18  /path/to/file.o"
_SUB_SEC = re.compile(
    r"^\s+(\S+)\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+(\S.+)$"
)


def parse_map(path: str):
    """
    Returns:
        section_totals -- dict[section_name] = total_size
        ram_by_obj     -- dict[obj_path] = bytes contributed to RAM sections
        flash_by_obj   -- dict[obj_path] = bytes contributed to flash sections
        all_by_obj     -- dict[obj_path] = bytes contributed to all sections
    """
    section_totals: dict = {}
    ram_by_obj: dict = defaultdict(int)
    flash_by_obj: dict = defaultdict(int)
    all_by_obj: dict = defaultdict(int)

    current_section: str = ""

    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.rstrip("\n")

            # Top-level section header must not start with whitespace.
            if not line.startswith((" ", "\t")):
                m = _SEC_HDR.match(line)
                if m:
                    sec_name = m.group(1)
                    try:
                        sec_size = int(m.group(3), 16)
                    except ValueError:
                        sec_size = 0
                    current_section = sec_name
                    if sec_name not in section_totals:
                        section_totals[sec_name] = sec_size
                    continue

            # Sub-section / object contribution line.
            m = _SUB_SEC.match(line)
            if m:
                sub_sec = m.group(1)
                try:
                    contrib_size = int(m.group(3), 16)
                except ValueError:
                    continue
                obj_path = m.group(4).strip()

                # Skip filler / linker-script meta entries.
                if sub_sec in ("*fill*", "COMMON", "LOAD", "OUTPUT"):
                    continue
                if obj_path.startswith("=") or obj_path.endswith((".ld", ".lds", ".x")):
                    continue
                if contrib_size == 0:
                    continue

                gov = current_section if current_section else sub_sec

                all_by_obj[obj_path] += contrib_size
                if _is_ram_section(gov) or _is_ram_section(sub_sec):
                    ram_by_obj[obj_path] += contrib_size
                elif _is_flash_section(gov) or _is_flash_section(sub_sec):
                    flash_by_obj[obj_path] += contrib_size

    return section_totals, ram_by_obj, flash_by_obj, all_by_obj


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description=(
            "Analyze a GNU linker .map file for ESP32 firmware. "
            "Use --ram-only to focus on actual internal-RAM consumers."
        )
    )
    parser.add_argument("mapfile", help="Path to the .map file to analyze")
    parser.add_argument(
        "threshold_kb",
        nargs="?",
        type=float,
        default=4.0,
        help="Warning threshold in kilobytes (default 4 KB)",
    )
    parser.add_argument(
        "--ram-only",
        action="store_true",
        help=(
            "Only report object-file contributions to internal-RAM sections "
            "(.dram0.data, .dram0.bss, .iram0.text, .noinit, .rtc.*). "
            "This is the correct mode to diagnose running out of RAM."
        ),
    )
    parser.add_argument(
        "--top",
        type=int,
        default=30,
        help="How many top entries to show (default 30)",
    )
    args = parser.parse_args()

    section_totals, ram_by_obj, flash_by_obj, all_by_obj = parse_map(args.mapfile)

    if not section_totals and not all_by_obj:
        print("No entries parsed from map file. Is the path correct?")
        sys.exit(1)

    threshold = int(args.threshold_kb * 1024)

    print(f"Analyzing map file: {args.mapfile}\n")

    # Section summary
    ram_secs   = {k: v for k, v in section_totals.items() if _is_ram_section(k)}
    flash_secs = {k: v for k, v in section_totals.items() if _is_flash_section(k)}
    other_secs = {k: v for k, v in section_totals.items()
                  if not _is_ram_section(k) and not _is_flash_section(k)}

    total_ram   = sum(ram_secs.values())
    total_flash = sum(flash_secs.values())

    print("=== Section totals ===")
    print(f"  Internal RAM total:  {human_size(total_ram)}")
    for sec, sz in sorted(ram_secs.items(), key=lambda x: x[1], reverse=True):
        print(f"    {sec:<30} {human_size(sz):>10}")
    print(f"  Flash total:         {human_size(total_flash)}")
    for sec, sz in sorted(flash_secs.items(), key=lambda x: x[1], reverse=True):
        print(f"    {sec:<30} {human_size(sz):>10}")
    if other_secs:
        print("  Other / unclassified:")
        for sec, sz in sorted(other_secs.items(), key=lambda x: x[1], reverse=True)[:10]:
            print(f"    {sec:<30} {human_size(sz):>10}")

    # Per-object breakdown
    if args.ram_only:
        by_obj = ram_by_obj
        label = "internal RAM"
    else:
        by_obj = all_by_obj
        label = "all sections (RAM + Flash combined -- use --ram-only for RAM analysis)"

    if not by_obj:
        print(
            "\nNo per-object contributions found in "
            + ("RAM sections." if args.ram_only else "the map file.")
        )
        print(
            "Tip: object contributions need sub-section lines like\n"
            '  " .dram0.bss  0x3fc...  0xSIZE  /path/to/file.o"\n'
            "in the map file."
        )
        return

    sorted_objs = sorted(by_obj.items(), key=lambda x: x[1], reverse=True)

    print(f"\n=== Top {args.top} contributors to {label} ===")
    for obj, sz in sorted_objs[: args.top]:
        flash_note = ""
        if not args.ram_only and obj in flash_by_obj:
            flash_note = f"  (flash: {human_size(flash_by_obj[obj])})"
        sn = short_name(obj)
        print(f"  {human_size(sz):>10}  {sn}{flash_note}")
        if obj != sn:
            print(f"             {obj}")

    print(f"\n=== Objects over {args.threshold_kb:.1f} KB in {label} ===")
    warn = False
    for obj, sz in sorted_objs:
        if sz < threshold:
            break
        warn = True
        sn = short_name(obj)
        print(f"  {human_size(sz):>10}  {sn}")
    if not warn:
        print("  <none above threshold>")


if __name__ == "__main__":
    main()
