#!/usr/bin/env python3
"""Patch ESP32_Display_Panel's esp_panel_drivers_conf.h to wrap
ESP_PANEL_DRIVERS_* defines with #ifndef guards so build-time -D
options don't trigger "redefined" warnings.

Idempotent: if a macro is already guarded the script will skip it.
"""
import os
import glob
import re
import sys

Import("env")

proj = env.get("PROJECT_DIR")
pioenv = env.get("PIOENV", "")

print(f"[patch] patch_display_panel_conf: PROJECT_DIR={proj!r}, PIOENV={pioenv!r}")

if not proj or not pioenv:
    print("[patch] missing PROJECT_DIR or PIOENV, skipping")
    sys.exit(0)

libdeps_root = os.path.join(proj, '.pio', 'libdeps', pioenv)
if not os.path.isdir(libdeps_root):
    print(f"[patch] libdeps root not found: {libdeps_root}, skipping")
    sys.exit(0)

panel_dirs = glob.glob(os.path.join(libdeps_root, 'ESP32_Display_Panel*'))
if not panel_dirs:
    print(f"[patch] ESP32_Display_Panel not found under {libdeps_root}, skipping")
    sys.exit(0)

for libroot in panel_dirs:
    conf_path = os.path.join(libroot, 'esp_panel_drivers_conf.h')
    if not os.path.isfile(conf_path):
        print(f"[patch] conf not found at {conf_path}, skipping")
        continue

    with open(conf_path, 'r', encoding='utf-8') as f:
        content = f.read()

    macros = set(re.findall(r"^\s*#\s*define\s+(ESP_PANEL_DRIVERS_[A-Z0-9_]+)", content, flags=re.M))
    if not macros:
        print(f"[patch] no ESP_PANEL_DRIVERS_ macros found in {conf_path}")
        continue

    changed = False
    # If a macro already has an #ifndef guard somewhere, skip it
    guarded = set(re.findall(r"#ifndef\s+(ESP_PANEL_DRIVERS_[A-Z0-9_]+)", content))

    lines = content.splitlines()
    out_lines = []
    seen = set()
    define_re = re.compile(r"^(\s*#\s*define\s+(ESP_PANEL_DRIVERS_[A-Z0-9_]+)\b\s*(.*))$")

    for ln in lines:
        m = define_re.match(ln)
        if m:
            full, macro, rest = m.group(1), m.group(2), m.group(3)
            if macro in seen or macro in guarded:
                out_lines.append(ln)
                continue
            # replace with guarded block
            out_lines.append(f"#ifndef {macro}")
            out_lines.append(f"#define {macro}{(' ' + rest) if rest else ''}")
            out_lines.append(f"#endif /* {macro} */")
            seen.add(macro)
            changed = True
        else:
            out_lines.append(ln)

    if changed:
        try:
            backup = conf_path + '.bak'
            if not os.path.exists(backup):
                with open(backup, 'w', encoding='utf-8') as bf:
                    bf.write(content)
            with open(conf_path, 'w', encoding='utf-8') as f:
                f.write('\n'.join(out_lines) + '\n')
            # print(f"[patch] patched {conf_path} (backup at {backup})")
        except Exception as e:
            print(f"[patch] failed to write patched file: {e}")
    else:
        print(f"[patch] no changes needed for {conf_path}")
