# A wireless CNC pendant with LVGL GUI

Very much a work in progress.

Uses two ESP32, one with screen for UX, the other as wireless-serial-bridge. Uses ESP-NOW as the wireless comms.

## In the flesh

Pendant in a 3D printed case:

![Pendant showing Jog Screen](/docs/images/vizi_pendant2s.jpeg)
![Pendant showing Probe Screen](/docs/images/vizi_pendant1s.jpeg)

Example Probing Screens:

<p align="center">
  <img src="https://github.com/thingsapart/esp32_visual_pendant/raw/main/docs/images/tab_probe_2ax_in.jpeg" width="33%" />
  <img src="https://github.com/thingsapart/esp32_visual_pendant/raw/main/docs/images/tab_probe_surf.jpeg" width="33%" />
  <img src="https://github.com/thingsapart/esp32_visual_pendant/raw/main/docs/images/tab_probe_3ax_msg.jpeg" width="33%" />
</p>

## Building

### Platformio

#### CLI usage

- [Platformio](https://docs.platformio.org/en/stable/core/installation/methods/installer-script.html)

#### VSCODE

- [VSCODE](https://code.visualstudio.com/docs/setup/setup-overview)
- [Platformio Extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)

### SDL2

#### MACOSX 
- `brew install sdl2`

#### Linux
- This is platform dependent

## Flashing

### Prerequisites

- UV python package manger (`apt-get install uv`, `brew install uv`, ...),
- esptool.py (`uv tool install esptool`).

#### WT32-SC01 Plus:

- Boot SC01 Plus into proramming mode (short GPIO0 / BOOT0 to GND before and during boot, then let go, esptool.py should now find the ESP32S3 - see online docs for pinout),
- `esptool.py --chip esp32s3 -b 460800 erase_flash`,
- `cd ./lib/micropython/ports/esp32` from lvgl_micropython main directory,
- ` esptool.py --chip esp32s3 -b 460800 --before default_reset --after no_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build-ESP32_GENERIC_S3/bootloader/bootloader.bin 0x8000 build-ESP32_GENERIC_S3/partition_table/partition-table.bin 0x10000 build-ESP32_GENERIC_S3/micropython.bin`,
- restart WT32-SC01 Plus `mpremote` should now be able to get into the micropython repl after boot.

#### Copy files

- Install `mpremote` (`uv tool install mpremote`).
- `mpremote connect /dev/<ttyid> fs cp -r * :`
- `mpremote connect /dev/<ttyid> reset`

(Or copy files one-by-one)

- `mpremote fs cp firmware/wt32_sc01_plus/encoder.py :encoder.py`,
- `mpremote fs cp firmware/wt32_sc01_plus/lv_style.py :lv_style.py`,
- `mpremote fs cp firmware/wt32_sc01_plus/rrf_machine.py :rrf_machine.py`,
- `mpremote fs cp firmware/wt32_sc01_plus/main.py :main.py`,
- ...
- `mpremote reset`.

## Running, Testing and Development

### Unix/Macos

- `cd firmware/wt32_sc01_plus`
- `lvgl_micropy_macOS main.py`

## Acknowledgements

- LVGL-micropython: https://github.com/lvgl-micropython/lvgl_micropython
- Google Fonts/Material Symbols used as images for probe buttons: https://developers.google.com/fonts/docs/material_symbols
