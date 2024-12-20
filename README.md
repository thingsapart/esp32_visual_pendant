# A wireless CNC pendant with LVGL GUI

Very much a work in progress.

Uses two ESP32, one with screen for UX, the other as wireless-serial-bridge. Uses ESP-NOW as the wireless comms.

## Building

### LVGL+Micropython 

#### WT32-SC01 Plus:

* `python3 make.py esp32 BOARD=ESP32_GENERIC_S3 DISPLAY=st7796 INDEV=ft6x36 --flash-size=16`

#### Unix/MacOS:

* `python3.13 make.py macOS  DISPLAY=sdl_display INDEV=sdl_pointer` or
* `python3.13 make.py unix  DISPLAY=sdl_display INDEV=sdl_pointer`

## Flashing

### Prerequisites

* UV python package manger (`apt-get install uv`, `brew install uv`, ...)
* esptool.py (`uv tool install esptool`)

#### WT32-SC01 Plus:

* `esptool.py --chip esp32s3 -b 460800 erase_flash`,
* `cd ./lib/micropython/ports/esp32` from lvgl_micropython main directory,
* ` esptool.py --chip esp32s3 -b 460800 --before default_reset --after no_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build-ESP32_GENERIC_S3/bootloader/bootloader.bin 0x8000 build-ESP32_GENERIC_S3/partition_table/partition-table.bin 0x10000 build-ESP32_GENERIC_S3/micropython.bin`

## Running, Testing and Development

### Unix/Macos

* `lvgl_micropy_macOS firmware/wt32_sc01_plus/main.py`

## Screenshots

![Main Screen](/docs/images/screen_main.jpeg)

![Jog Screen](/docs/images/screen_jog.jpeg)

