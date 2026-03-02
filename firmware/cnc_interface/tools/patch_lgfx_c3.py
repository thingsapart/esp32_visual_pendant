"""
patch_lgfx_c3.py — PlatformIO pre-build extra_script
=====================================================
Applies two in-place patches to downloaded library sources for the
ESP32-C3 (RISC-V) target.  Both patches are idempotent.

Patch A — LovyanGFX  lgfx/v1/platforms/esp32/common.cpp
---------------------------------------------------------
GPIO register struct fields were renamed in ESP-IDF 5.x for RISC-V targets:

    .in_sel   →  .func_sel   (gpio_func_in_sel_cfg_reg_t)
    .out_sel  →  .func_sel   (gpio_func_out_sel_cfg_reg_t)

The Xtensa (ESP32/S2/S3) names are preserved via #if guards so the same
patched file still compiles on other targets.

Patch B — LVGL  src/osal/lv_freertos.c
---------------------------------------
lv_freertos.c does  #include "atomic.h"  expecting the FreeRTOS
FreeRTOS-Kernel/include/freertos/ directory to be directly on the compiler
search path.  On the Xtensa/S3 Arduino framework this include dir is added
automatically; on the RISC-V/C3 framework it is not.

The header lives at  <freertos/atomic.h>  which IS on the search path for
all targets (the arduino-libs package exposes freertos/ as a top-level
include directory).  Changing the include to "freertos/atomic.h" fixes the
missing-header error without touching lv_conf.h or disabling FreeRTOS
support in LVGL.
"""

import pathlib
import re

Import("env")  # type: ignore  # PlatformIO / SCons injects this

LIBDEPS_DIR = env.subst("$PROJECT_LIBDEPS_DIR")
ENV_NAME    = env.subst("$PIOENV")

# ── Patch A: LovyanGFX ────────────────────────────────────────────────────────

LGFX_FILE = pathlib.Path(
    LIBDEPS_DIR, ENV_NAME,
    "LovyanGFX", "src", "lgfx", "v1", "platforms", "esp32", "common.cpp"
)

LGFX_MARKER = "// [patch_lgfx_c3] IDF5 C3 field rename"

RISC_V_GUARD = (
    "#if defined(CONFIG_IDF_TARGET_ESP32C3) || "
    "defined(CONFIG_IDF_TARGET_ESP32C2) || "
    "defined(CONFIG_IDF_TARGET_ESP32H2)  " + LGFX_MARKER
)

def patch_lgfx():
    if not LGFX_FILE.exists():
        print("[patch_lgfx_c3] LovyanGFX common.cpp not found — will retry after library install.")
        return

    source = LGFX_FILE.read_text(encoding="utf-8")
    if LGFX_MARKER in source:
        print("[patch_lgfx_c3] LovyanGFX: already patched.")
        return

    patched = source

    OLD_IN = "GPIO.func_in_sel_cfg[peripheral_sig].in_sel"
    if OLD_IN in patched:
        patched = re.sub(
            r"([ \t]*)uint32_t result = " + re.escape(OLD_IN) + r"\s*;",
            RISC_V_GUARD + "\n"
            r"\g<1>    uint32_t result = GPIO.func_in_sel_cfg[peripheral_sig].func_sel;\n"
            "#else\n"
            r"\g<1>    uint32_t result = " + OLD_IN + ";\n"
            "#endif",
            patched, count=1,
        )
        print("[patch_lgfx_c3] LovyanGFX: patched in_sel -> func_sel.")
    else:
        print("[patch_lgfx_c3] LovyanGFX: in_sel not found — may be fixed upstream.")

    OLD_OUT = "GPIO.func_out_sel_cfg[pin].out_sel"
    if OLD_OUT in patched:
        patched = re.sub(
            r"([ \t]*)" + re.escape(OLD_OUT) + r"\s*=\s*SIG_GPIO_OUT_IDX\s*;",
            RISC_V_GUARD + "\n"
            r"\g<1>    GPIO.func_out_sel_cfg[pin].func_sel = SIG_GPIO_OUT_IDX;\n"
            "#else\n"
            r"\g<1>    " + OLD_OUT + " = SIG_GPIO_OUT_IDX;\n"
            "#endif",
            patched, count=1,
        )
        print("[patch_lgfx_c3] LovyanGFX: patched out_sel -> func_sel.")
    else:
        print("[patch_lgfx_c3] LovyanGFX: out_sel not found — may be fixed upstream.")

    if patched != source:
        LGFX_FILE.write_text(patched, encoding="utf-8")
        print("[patch_lgfx_c3] LovyanGFX: wrote patched file.")
    else:
        print("[patch_lgfx_c3] LovyanGFX: no changes needed.")


# ── Patch B: LVGL lv_freertos.c ──────────────────────────────────────────────

LVGL_FILE = pathlib.Path(
    LIBDEPS_DIR, ENV_NAME,
    "lvgl", "src", "osal", "lv_freertos.c"
)

LVGL_MARKER = "// [patch_lgfx_c3] freertos/atomic.h"

def patch_lvgl_freertos():
    if not LVGL_FILE.exists():
        print("[patch_lgfx_c3] LVGL lv_freertos.c not found — will retry after library install.")
        return

    source = LVGL_FILE.read_text(encoding="utf-8")
    if LVGL_MARKER in source:
        print("[patch_lgfx_c3] LVGL lv_freertos.c: already patched.")
        return

    OLD = '#include "atomic.h"'
    NEW = '#include "freertos/atomic.h"  ' + LVGL_MARKER

    if OLD not in source:
        print("[patch_lgfx_c3] LVGL lv_freertos.c: '" + OLD + "' not found — may be fixed upstream.")
        return

    patched = source.replace(OLD, NEW, 1)
    LVGL_FILE.write_text(patched, encoding="utf-8")
    print("[patch_lgfx_c3] LVGL lv_freertos.c: patched atomic.h -> freertos/atomic.h.")


patch_lgfx()
patch_lvgl_freertos()

