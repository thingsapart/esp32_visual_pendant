# Tasmota Platform Build Process

Reference for how the Tasmota/pioarduino `espressif32` platform builds Arduino+IDF
firmware, based on source analysis of:

- `~/.platformio/platforms/espressif32@src-<hash>/builder/frameworks/espidf.py`
- `~/.platformio/platforms/espressif32@src-<hash>/builder/frameworks/arduino.py`

---

## Overview

When `framework = arduino` is used, the Tasmota platform runs a **two-stage build**:

1. **IDF/Arduino library compilation** — invoked by `arduino.py` which calls into
   `espidf.py`.  This stage uses CMake + Ninja (ESP-IDF's native build system) to
   produce pre-compiled static libraries.  PlatformIO's project `build_flags` are
   **not** passed here (see below).

2. **Project source compilation** — SCons compiles the project's own `.c/.cpp`
   files with the usual PlatformIO `build_flags`.  The compiled IDF/Arduino libs
   from stage 1 are linked in at the end.

---

## `custom_sdkconfig`

### Purpose

Applies per-environment overrides to the master `sdkconfig.defaults` file before
CMake runs.  The master source is the baked-in file shipped with the Arduino IDF
libs:

```
~/.platformio/packages/framework-arduinoespressif32/
    tools/esp32-arduino-libs/<mcu>/sdkconfig
```

### Mechanism (espidf.py `HandleArduinoIDFsettings`)

1. `env.GetProjectOption("custom_sdkconfig")` is split into lines.
2. Each line that is a `file://./path` reference is read and prepended to the
   flag list.
3. The master `sdkconfig` is read line-by-line.  For each line, the function
   `get_flag()` extracts a symbol key:
   - `CONFIG_FOO=bar` → key is `CONFIG_FOO`
   - `# CONFIG_FOO is not set` → key is `CONFIG_FOO`
   - comment/blank lines → `None` (passed through unchanged)
4. If a key matches a line in `custom_sdkconfig`, the source line is **replaced**
   with the override value.  Non-matching extras are **appended** at the end.
5. The result is written to `<project_dir>/sdkconfig.defaults`.

### Valid syntax

```ini
custom_sdkconfig =
    CONFIG_FOO=y           ; enable
    CONFIG_FOO=n           ; disable (written literally; CMake/Kconfig accepts it)
    # CONFIG_FOO is not set ; canonical Kconfig disable form
```

**IMPORTANT — PlatformIO INI parsing strips `#` lines.**  A line beginning with
`#` inside a multi-line INI value is treated as an INI comment and is silently
dropped before `env.GetProjectOption()` returns.  Use `=n` instead of the
comment form when writing `custom_sdkconfig` entries in `platformio.ini`.

`file://./relative/path.ini` entries cause the **entire file** to be substituted
as the flag list (the loop returns after the first file match).  All `custom_sdkconfig`
inline entries after a `file://` line are **ignored**.

### Limitations

- `custom_sdkconfig` only affects the sdkconfig **values**.  It cannot remove a
  Kconfig option that is forced on by a `select` directive in another component.
- When `custom_sdkconfig` is present, `arduino.py` sets `BUILD_FLAGS=""` in the
  SCons environment before the IDF cmake build.  **Project `build_flags` (`-D`,
  `-U`, etc.) are therefore invisible to Arduino core / IDF component compilation.**
  `-U` undefines added to `build_flags` will NOT suppress macros defined in the
  pre-built `sdkconfig.h`.

### Caching / rebuild trigger

The platform re-runs cmake and recompiles IDF libs only when the MD5 hash of
`custom_sdkconfig` changes (written as the first line of `sdkconfig.defaults`).
Changing `custom_sdkconfig` reliably triggers a rebuild; changing ordinary
`build_flags` alone does not.

---

## `custom_component_remove`

### Purpose

Removes entries from the Arduino framework's `idf_component.yml` **before cmake
runs**, so that their Kconfig files are never loaded.  This is the only reliable
way to disable a component whose Kconfig symbol is forced on by a `select`
elsewhere.

### Mechanism (`HandleCOMPONENTsettings` in espidf.py)

1. Locates `idf_component.yml` (first tries the Arduino framework dir, then the
   project `src/` dir).
2. Backs it up to `idf_component.yml.orig`.
3. Parses it as YAML/JSON, deletes the named keys from `dependencies`, and
   writes it back.
4. CMake + component manager runs subsequently with the reduced dependency list.

### Syntax

```ini
custom_component_remove =
    espressif/esp_hosted
    espressif/esp_wifi_remote
```

Component names must match the `dependencies:` keys in `idf_component.yml`
exactly (typically `vendor/name`).

### `custom_component_add`

Symmetric: adds new `{name: {version: "*"}}` entries.  Use `name@version` to pin:

```ini
custom_component_add =
    espressif/esp_hosted@2.11.0
```

---

## `custom_sdkconfig` vs `custom_component_remove` — decision guide

| Scenario | Tool |
|---|---|
| Override a numeric/string config value | `custom_sdkconfig` |
| Enable/disable a bool config that is not forced by `select` | `custom_sdkconfig` |
| Disable a component whose Kconfig is force-selected by another component or by a chip-level default | `custom_component_remove` |
| Prevent a component's source files and Kconfig from entering the build entirely | `custom_component_remove` |
| Override a `#define` that appears in a **pre-built** `sdkconfig.h` (not regenerated) | Neither — patch `sdkconfig.h` directly, or add `custom_sdkconfig` entries that cause the IDF libs to be recompiled |

---

## Pre-built `sdkconfig.h` vs generated `sdkconfig`

There are **two distinct copies** of the sdkconfig for ESP32-P4 (Arduino target):

| File | When written | Used by |
|---|---|---|
| `tools/esp32-arduino-libs/esp32p4_es/<variant>/include/sdkconfig.h` | Shipped with the framework package, never regenerated unless IDF libs are recompiled from scratch | gcc `#include` during Arduino core and IDF component compilation |
| `<project_dir>/sdkconfig.defaults` | Written by `HandleArduinoIDFsettings` at each build | CMake/Kconfig to generate `<build_dir>/config/sdkconfig` |

For Arduino targets (`framework = arduino`), `custom_sdkconfig` modifies
`sdkconfig.defaults` and triggers a rebuild of the IDF libs — **including
regenerating `sdkconfig.h`** — only when the hash changes.  Once the IDF libs
are up to date, subsequent builds reuse the cached `.a` files and `sdkconfig.h`
unchanged.

If `sdkconfig.h` in the framework package predates the `custom_sdkconfig` change
(e.g. because the build cache was left intact), the macros in the header will
still reflect the old values.  Delete `.pio/build/<env>/` to force a full cmake
reconfigure.

---

## Why `build_flags` `-U`/`-D` cannot suppress `sdkconfig.h` macros

The `#define CONFIG_ESP_WIFI_REMOTE_ENABLED 1` and similar macros in
`sdkconfig.h` are included via `-include sdkconfig.h` in almost every IDF
component's compile command (recorded in the cmake code model).  PlatformIO
project `build_flags` are applied to the **SCons project env** only.  When
`custom_sdkconfig` is present, `arduino.py` explicitly replaces `BUILD_FLAGS`
with `""` for the IDF/Arduino cmake compilation path (line ~375 of `espidf.py`),
so `-U` flags in `build_flags` have no effect on Arduino core files.

---

## Case study: `esp32-hal-hosted.c` on ESP32-P4

### Symptom

```
error: 'CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET' undeclared (first use in this function)
```

### Cause chain

1. `esp32p4_es/sdkconfig.h` (pre-built) has `#define CONFIG_ESP_WIFI_REMOTE_ENABLED 1`
   and `#define CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE 1`.
2. `esp32-hal-hosted.c` line 16: `#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) || defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)` → body is compiled.
3. Body references `CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET` (an old string Kconfig
   option).
4. Project uses `espressif/esp_hosted` ≥ v2.12: that version replaced
   `config ESP_HOSTED_IDF_SLAVE_TARGET string` with
   `choice ESP_HOSTED_CP_TARGET` bools → symbol is never defined in the
   regenerated `sdkconfig.h`.

### Fix

```ini
custom_component_remove =
    espressif/esp_hosted
    espressif/esp_wifi_remote
```

Removing these from `idf_component.yml` means their Kconfig is never parsed,
`CONFIG_ESP_WIFI_REMOTE_ENABLED` is never selected, and the body of
`esp32-hal-hosted.c` is never compiled.

### Why `custom_sdkconfig = CONFIG_ESP_WIFI_REMOTE_ENABLED=n` doesn't work

The ESP32-P4 chip-level Kconfig (`${IDF_HOST}/socs/esp32p4/Kconfig.soc_caps.in`)
contains a `select ESP_WIFI_REMOTE_ENABLED` that fires unconditionally.  Writing
`=n` into `sdkconfig.defaults` is overridden by the `select` during Kconfig
resolution.

### Why `-UCONFIG_ESP_WIFI_REMOTE_ENABLED` in `build_flags` doesn't work

See the preceding section: `BUILD_FLAGS` is zeroed out for IDF/Arduino
compilation and the pre-built `sdkconfig.h` `#define` remains visible.

---

## `board_build.cmake_extra_args`

Arbitrary cmake arguments can be injected via:

```ini
board_build.cmake_extra_args = -DCMAKE_C_FLAGS_EXTRA="-Dfoo=1"
```

These are appended to the cmake invocation in `run_cmake()`.  This is a lower-
level escape hatch when the higher-level mechanisms are insufficient.

---

## Rebuild triggers summary

| Change | Triggers IDF lib rebuild? |
|---|---|
| `custom_sdkconfig` content | Yes (MD5 hash check) |
| `build_flags` only | No |
| `custom_component_remove` / `custom_component_add` | Modifies `idf_component.yml` which is a cmake input — yes |
| `sdkconfig.defaults` mtime newer than `CMakeCache.txt` | Yes |
| Manual delete of `.pio/build/<env>/` | Yes (full reconfigure) |
