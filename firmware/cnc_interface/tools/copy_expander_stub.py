#!/usr/bin/env python3
"""Pre-build script that installs our local esp_io_expander stubs into
the ESP32_Display_Panel library tree so quoted includes resolve during
library compilation.

The project contains minimal stub headers under `include/esp_io_expander/`.
PlatformIO sometimes doesn't add the project include path to library
compilation, so copy the stub headers into the panel library's `src`/`include`
folders when present. The script is tolerant if the libdeps folder isn't
created yet.
"""

import os
import glob
import shutil
import sys

Import("env")

proj = env.get("PROJECT_DIR")
pioenv = env.get("PIOENV", "")
# print(f"[patch] copy_expander_stub.py: PROJECT_DIR={proj!r}, PIOENV={pioenv!r}")

if not proj or not pioenv:
    print("[patch] missing PROJECT_DIR or PIOENV from env, skipping script")
    sys.exit(0)

marker = os.path.join(proj, ".pio", "expander_stub_ran")
try:
    os.makedirs(os.path.dirname(marker), exist_ok=True)
    with open(marker, "w") as f:
        f.write("ran\n")
except Exception:
    pass

stub_dir = os.path.join(proj, "include", "esp_io_expander")
if not os.path.isdir(stub_dir):
    print(f"[patch] stub directory not found: {stub_dir}, skipping")
    sys.exit(0)

# Mapping from source filenames (in our stub dir) to target filenames
# expected by ESP32_Display_Panel headers.
files_map = {
    os.path.join(stub_dir, "port", "esp_io_expander.h_"):
        os.path.join("port", "esp_io_expander.h"),
    os.path.join(proj, "include", "chip", "esp_expander_base.hpp"):
        os.path.join("chip", "esp_expander_base.hpp"),
}

# special handling for the C++ stub: install one master copy at the lib root
# and create thin wrappers in include/ and src/ that forward-include it so
# the master is the single place that defines symbols (avoids duplicate defs).
master_stub = os.path.join(stub_dir, "esp_io_expander.hpp_")


# Locate possible panel library roots under .pio/libdeps/<env>/
libdeps_root = os.path.join(proj, ".pio", "libdeps", pioenv)
if not os.path.isdir(libdeps_root):
    print(f"[patch] libdeps root not present yet: {libdeps_root}, skipping copy")
    sys.exit(0)

panel_lib_glob = os.path.join(libdeps_root, "ESP32_Display_Panel*")
targets = glob.glob(panel_lib_glob)
if not targets:
    print(f"[patch] no ESP32_Display_Panel library found under {libdeps_root}")
    sys.exit(0)

# print(f"[patch] found panel library directories: {targets}")

for libroot in targets:
    # ensure common locations exist (src/ and include/) and copy into both
    candidate_dirs = []
    for sub in ("src", "include"):
        p = os.path.join(libroot, sub)
        os.makedirs(p, exist_ok=True)
        candidate_dirs.append(p)
    # also keep lib root as a fallback
    candidate_dirs.append(libroot)

    # first, install the master C++ stub (if present)
    if os.path.isfile(master_stub):
        master_dest = os.path.join(libroot, "esp_io_expander.hpp")
        try:
            shutil.copy(master_stub, master_dest)
            # print(f"[patch] copied master {master_stub} -> {master_dest}")
        except Exception as e:
            print(f"[patch] failed to copy master {master_stub} -> {master_dest}: {e}")

        # write thin wrappers in include/ and src/ that forward-include the master
        for wrapper_sub in ("include", "src"):
            wrapper_dir = os.path.join(libroot, wrapper_sub)
            os.makedirs(wrapper_dir, exist_ok=True)
            wrapper_path = os.path.join(wrapper_dir, "esp_io_expander.hpp")
            try:
                with open(wrapper_path, "w") as wf:
                    wf.write("#pragma once\n#include \"../esp_io_expander.hpp\"\n")
                # print(f"[patch] wrote wrapper {wrapper_path}")
            except Exception as e:
                print(f"[patch] failed to write wrapper {wrapper_path}: {e}")

    # then copy the remaining mapped files (C header and chip stub)
    for src_path, target_name in files_map.items():
        if not os.path.isfile(src_path):
            print(f"[patch] stub source missing: {src_path}, skipping")
            continue
        for dest_dir in candidate_dirs:
            dest = os.path.join(dest_dir, target_name)
            # ensure destination directory exists (supports nested paths)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            try:
                shutil.copy(src_path, dest)
                # print(f"[patch] copied {src_path} -> {dest}")
            except Exception as e:
                print(f"[patch] failed to copy {src_path} -> {dest}: {e}")
