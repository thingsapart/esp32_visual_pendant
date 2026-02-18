# ESP32-C6 ESP-NOW Bridge Firmware

This is the companion firmware that runs on the **ESP32-C6 sidecar chip** present on
ESP32-P4 + C6 boards (e.g. JC8012P4A1, JC1060P470).

It bridges ESP-NOW wireless packets to a binary-framed UART and relays them to the
ESP32-P4 main chip, making wireless (ESP-NOW hub communication) available to P4-based
pendant builds even though the P4 itself has no radio.

---

## Background

The ESP32-P4 SoC has no built-in Wi-Fi or Bluetooth.  Boards in the Guition/JCZN
family pair the P4 with an ESP32-C6 sidecar connected via **SDIO 4-bit @ 40 MHz**.
The factory firmware on the C6 (`esp-hosted-mcu`) exposes Wi-Fi infrastructure mode
to the P4 but does **not** support ESP-NOW (open upstream issue as of Feb 2026).

Rather than waiting for upstream, this firmware replaces the C6 with a thin
ESP-NOW ↔ UART relay.  The C6:
1. Listens for incoming ESP-NOW broadcasts from the wireless hub.
2. Forwards them to the P4 over UART using a 12-byte-overhead binary frame format.
3. Receives outgoing frames from the P4 over the same UART.
4. Sends them via ESP-NOW to the previously discovered hub.

The P4 application uses the normal `remote_wrapper_*` API — no changes needed in
the ui/machine layers.

---

## Wiring

Only **two signal wires** are needed.  The grounds are already shared on-board.

```
  JC8012 board
  ┌─────────────────────────────┐
  │  PROG_C6 header (6-pin)     │
  │  TXD  (C6 GPIO16, UART0 TX) ├────────────── P4 GPIO5  (UART2 RX)
  │  RXD  (C6 GPIO17, UART0 RX) ├────────────── P4 GPIO4  (UART2 TX)
  │  GND                        │  (shared, no wire needed)
  │  EN   (C6 reset)            │
  │  IO0  (C6 boot mode)        │
  └─────────────────────────────┘
```

> **Note:** C6 `TXD` → P4 `RX`, and C6 `RXD` ← P4 `TX`.  If data is not flowing,
> swap the two wires — it is the most common wiring mistake.

The P4 GPIO numbers above are the defaults set in
`cnc_interface/platformio.ini` for `env:display-jc8012p4a1`.  Change
`C6_BRIDGE_UART_TX` / `C6_BRIDGE_UART_RX` there if you use different GPIOs.

---

## How to flash the C6

The C6 is flashed independently of the P4 via the `PROG_C6` header.

### Hardware
Connect a USB-to-UART adapter (e.g. ESP-Prog, CP2102, CH340) to the header:

| PROG_C6 pin | USB-UART adapter |
|---|---|
| TXD | RX |
| RXD | TX |
| GND | GND |
| EN  | DTR (or manual reset) |
| IO0 | RTS (or pull to GND to enter bootloader) |

### Entering bootloader mode
Hold **IO0 low** while briefly asserting **EN** (reset).  The C6 will boot into
the UART download mode.

### Flashing with PlatformIO
```bash
cd firmware/c6_espnow_bridge
pio run --target upload --environment c6_bridge
```

Set the correct upload port in `platformio.ini` or via `--upload-port`:
```bash
pio run --target upload --environment c6_bridge --upload-port /dev/tty.usbserial-XXXX
```

### After flashing
Disconnect IO0, press EN.  The C6 will boot the bridge firmware and start
listening for ESP-NOW packets within a few hundred milliseconds.

---

## Configuration

All knobs are set via `build_flags` in `platformio.ini`:

| Flag | Default | Description |
|---|---|---|
| `C6_BRIDGE_UART_NUM` | `0` | Arduino UART port number |
| `C6_BRIDGE_UART_TX` | `16` | C6 TX GPIO (→ P4 RX) |
| `C6_BRIDGE_UART_RX` | `17` | C6 RX GPIO (← P4 TX) |
| `C6_BRIDGE_UART_BAUD` | `921600` | Baud rate |
| `C6_BRIDGE_WIFI_CHANNEL` | `1` | ESP-NOW Wi-Fi channel |

The Wi-Fi channel must match the hub.  The hub ESP32-S3 typically runs on
channel 1 when started in STA mode without an AP.  If you see no traffic,
try channels 6 or 11.

---

## Frame protocol

The UART link uses a compact binary frame with XOR-based integrity check:

```
Offset  Size  Field
------  ----  -----
0       1     SOF0  = 0xAB
1       1     SOF1  = 0xCD
2       1     DIR   0x01 = C6→P4 (received from ESP-NOW)
                    0x02 = P4→C6 (to send via ESP-NOW)
3       6     MAC   source MAC (DIR=0x01) or destination MAC (DIR=0x02)
9       2     LEN   payload length, little-endian uint16 (1–250)
11      LEN   DATA  raw ESP-NOW payload
11+LEN  1     CRC8  XOR of MAC[0..5] ^ LEN_LO ^ LEN_HI ^ DATA[0..LEN-1]
```

Total overhead: **12 bytes** per frame.  Maximum frame: **262 bytes**.

---

## Debugging

The C6 firmware prints diagnostic messages to the **USB CDC** console
(`ARDUINO_USB_CDC_ON_BOOT=1`), separate from the bridge UART:

```
[BRIDGE] Booting ESP32-C6 ESP-NOW bridge...
[BRIDGE] WiFi channel = 1
[BRIDGE] C6 MAC: AA:BB:CC:DD:EE:FF
[BRIDGE] Ready. Bridge UART: TX=GPIO16 RX=GPIO17 @921600 baud
[BRIDGE] Added peer 12:34:56:78:9A:BC
```

To watch C6 logs while the bridge is running, open the C6's USB CDC port
(separate from the P4's USB port) e.g.:
```bash
pio device monitor --port /dev/tty.usbmodemXXXX --baud 115200
```

---

## P4-side integration

The P4 pendant firmware is built with `env:display-jc8012p4a1` which sets:

```ini
-D REMOTE_COMMS_C6_BRIDGE
-D C6_BRIDGE_UART_NUM=2
-D C6_BRIDGE_UART_TX=4
-D C6_BRIDGE_UART_RX=5
-D C6_BRIDGE_UART_BAUD=921600
```

These activate the `#elif defined(ESP32P4_HW) && defined(REMOTE_COMMS_C6_BRIDGE)`
code path in `src/driver/remote_comms_wrapper.c`, which initialises UART2 and
spawns a receive task.  All `remote_wrapper_*` calls behave identically to the
native ESP-NOW path on S3/S2/classic-ESP32 builds.
