#### A. Drop-in ESP-NOW API Replacement (The Interface)
Create a component exposing the ESP-NOW interface specifically for the P4 build target (`jc8012p4a1`). It will expose the standard ESP-IDF signature but route internally to the SDIO bridge:
- **`esp_now_init()`**: Initializes the SDIO host (`essl_sdio`), performs the P4/C6 handshake, and spawns the RX FreeRTOS task.
- **`esp_now_send()`**: Wraps payload in the bridge protocol (Header + Payload + CRC) and pushes it to the SDIO TX queue.
- **`esp_now_register_recv_cb()`**: Stores the standard `esp_now_recv_cb_t` pointer. When the SDIO RX task receives a valid ESP-NOW frame from the C6, it constructs a fake `esp_now_recv_info_t` using the MAC address from the protocol header and calls this callback.
- **`esp_now_register_send_cb()`**: Subscribes to TX status reports returning from the C6 (requires updating the bridge protocol to relay `esp_now_send_cb_t` events back to the P4).

#### B. Robust Handshake & Reboot Handling
To handle reboots gracefully, the C6 and P4 must maintain a synchronized state machine without leaking data into the void:
1. **Heartbeat / Sync Symbol**: The P4 periodically sends a sync byte (e.g., `?` or `SYNC`) over SDIO. 
2. **C6 TX Gating**: The C6 must buffer or drop wireless ESP-NOW RX packets and delay any slave-to-host transmissions until it receives this sync byte from the P4. 
3. **Hard Reset Line**: If possible, tie a P4 GPIO to the C6's EN (Reset) pin. When the P4 boots, it can hardware-reset the C6 to guarantee a clean initial state.

#### C. Association and Connection State
Instead of the C6 guessing the connection state, make the C6 a true dumb pipe:
- Pass `esp_now_add_peer()`, `esp_now_del_peer()`, and Wi-Fi channel configuration from the P4 down to the C6 over SDIO using dedicated control frames (e.g., `DIR = 0x03` for Control).
- When a connection state changes (e.g., the hub is found or lost), the C6 sends a control frame back to the P4, which the mock API translates into standard Wi-Fi/ESP-NOW connect/disconnect events for `machine_interface`.

#### D. Packet Routing
The C6 just forwards bytes with minimal inspection:
- **P4 to C6 (TX)**: P4 `esp_now_send()` -> Frame -> SDIO -> C6 un-frames -> Native `esp_now_send()`.
- **C6 to P4 (RX)**: C6 native `esp_now_recv_cb()` -> Frame (include MAC, RX RSSI) -> SDIO -> P4 un-frames -> Mock `esp_now_recv_cb()`.
- **TX Status Feedback**: C6 native `esp_now_send_cb()` -> Control Frame (MAC + Status) -> SDIO -> P4 Mock `esp_now_send_cb()`.