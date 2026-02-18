// remote_comms_wrapper.c

#include "remote_comms_wrapper.h"

#if defined(ESP32_HW) && !defined(ESP32P4_HW)

#include <assert.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <string.h>

#include "debug.h"

static const char *TAG = "remote_comms_wrapper";

static const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Forward declarations of static callback functions
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);
static void on_data_recv(const esp_now_recv_info_t *esp_now_info,
                         const uint8_t *data, int data_len);

// Store callback functions
static remote_wrapper_recv_cb_t g_recv_cb = NULL;
static remote_wrapper_send_cb_t g_send_cb = NULL;
static void *g_user_data = NULL;

static void wifi_init() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  // In a real application, you might connect to an access point here.
  // For simplicity, we'll just start Wi-Fi in station mode.
}

#define USE_SEND_QUEUE

#ifdef USE_SEND_QUEUE
#define QUEUE_LENGTH 2
#define TASK_PRIORITY (tskIDLE_PRIORITY + 1)

typedef struct {
  uint8_t mac[6];
  uint8_t data_len;
  uint8_t data[ESP_NOW_MAX_DATA_LEN];
} remote_send_queue_item_t;

void remote_send_task(void *args) {
  QueueHandle_t queue = (QueueHandle_t)args;
  bool abort = false;

  while (!abort) {
    // Block indefinitely waiting for a notification from the queue
    remote_send_queue_item_t item;
    if (xQueueReceive(queue, &item, portMAX_DELAY) == pdTRUE) {
      bool res = remote_wrapper_send_now(item.mac, item.data, item.data_len);
      if (res) {
        LOGI(TAG, "Sent remote message len %d [OK]", item.data_len);
      } else {
        LOGW(TAG, "Failed to send remote message len %d", item.data_len);
      }

      // Delay a little to avoid ESP_ERR_ESP_NOW_NO_MEM.
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }

  LOGW(TAG, "remote_send_task ended unexpectedly.");
}
QueueHandle_t remote_send_queue = NULL;

TaskHandle_t remote_send_task_run() {
  QueueHandle_t queue =
      xQueueCreate(QUEUE_LENGTH, sizeof(remote_send_queue_item_t));
  if (queue == NULL) {
    LOGE(TAG, "Failed to create serial received notification queue!");
    return false;
  }

  TaskHandle_t task_handle;
  BaseType_t task_created =
      xTaskCreatePinnedToCore(remote_send_task,
                              "remote_send_task",  // Task name
                              1024 * 4,            // Stack depth
                              queue,  // Parameter passed to the task (using
                                      // global s_machine_interface instead)
                              TASK_PRIORITY,  // Task priority
                              &task_handle,   // Task handle
                              TASK_MACHINE_STATE_PROC_CORE);
  LOGI(TAG, "Creating task: %s => %p, queue %p", "remote_send_task",
       task_handle, queue);

  if (task_created != pdPASS) {
    LOGE(TAG, "Failed to create remote send task (%d)!", task_created);
    vQueueDelete(queue);
    return NULL;
  }

  LOGI(TAG, "Remote send task started successfully.");

  remote_send_queue = queue;

  return task_handle;
}

bool remote_wrapper_send(const uint8_t *mac_addr, const uint8_t *data,
                         size_t len) {
  if (remote_send_queue == NULL) {
    LOGW(TAG, "Remote send queue NULL - not sending!");
    return false;
  }

  remote_send_queue_item_t item = {0};
  item.data_len = len;
  memcpy(&item.mac[0], mac_addr, sizeof(item.mac));
  memcpy(&item.data[0], data, len);

  BaseType_t res = xQueueSend(remote_send_queue, &item, 0);
  if (res != pdTRUE) {
    LOGW(TAG, "Remote send queue most likely full.");
    return false;
  } else {
    LOGV(TAG, "Remote send queued: %d", len);
  }

  return true;
}

#else

bool remote_wrapper_send(const uint8_t *mac_addr, const uint8_t *data,
                         size_t len) {
  return remote_wrapper_send_now(mac_addr, data, len);
}

#endif

bool remote_wrapper_init(remote_wrapper_recv_cb_t recv_cb,
                         remote_wrapper_send_cb_t send_cb, void *user_data) {
  // Initialize NVS (needed for Wi-Fi)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init();

  // Initialize ESP-NOW
  ESP_ERROR_CHECK(esp_now_init());

  g_user_data = user_data;
  g_recv_cb = recv_cb;
  g_send_cb = send_cb;

  // Register callbacks
  ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

#ifdef USE_SEND_QUEUE
  if (!remote_send_task_run()) {
    LOGE(TAG, "Failed to start remote send task");

    return false;
  }
#endif

  return true;
}

bool remote_wrapper_add_peer(const uint8_t *mac_addr) {
  esp_now_peer_info_t peer_info;
  memset(&peer_info, 0, sizeof(peer_info));
  memcpy(peer_info.peer_addr, mac_addr, 6);
  peer_info.channel = 0;          // Use the default channel
  peer_info.ifidx = WIFI_IF_STA;  // Use STA interface
  peer_info.encrypt = false;      // No encryption for simplicity

  // Add the peer
  esp_err_t res = ESP_OK;
  if ((res = esp_now_add_peer(&peer_info)) != ESP_OK) {
    LOGE(TAG, "Failed to add peer %d", res);
    return false;
  }
  return true;
}

bool remote_wrapper_add_peer_if_not_known(const uint8_t *received_mac_addr,
                                          uint8_t *stored_mac_addr) {
  // Check if the stored MAC address is all zeros (uninitialized)
  bool is_uninitialized = memcmp(stored_mac_addr, "\0\0\0\0\0\0", 6) == 0 ||
                          memcmp(stored_mac_addr, broadcast_mac, 6);
  LOGI(TAG,
       "RECV hub MAC address " MACSTR " == new MAC " MACSTR " => is_unknown %d",
       MAC2STR(stored_mac_addr), MAC2STR(received_mac_addr), is_uninitialized);

  // If uninitialized, or if the received MAC matches the stored MAC, add/update
  // the peer
  if (is_uninitialized || memcmp(received_mac_addr, stored_mac_addr, 6) == 0) {
    // Add or update the peer
    if (!remote_wrapper_add_peer(received_mac_addr)) {
      return false;
    }

    // Update the stored MAC address *only* if it was uninitialized
    if (is_uninitialized) {
      memcpy(stored_mac_addr, received_mac_addr, 6);
      LOGI(TAG, "[%s] Learned hub MAC address: " MACSTR, TAG,
           MAC2STR(stored_mac_addr));
    }
    return true;  // peer existed or added.
  } else {
    // The received MAC address is different from the stored one. This is
    // unexpected.
    LOGI(TAG,
         "Received broadcast from a different hub!  Stored: " MACSTR
         ", Received: " MACSTR,
         TAG, MAC2STR(stored_mac_addr), MAC2STR(received_mac_addr));
    // Do NOT add the peer.  We don't want to overwrite our existing peer.
    return false;
  }
}

bool remote_wrapper_send_now(const uint8_t *mac_addr, const uint8_t *data,
                             size_t len) {
  assert(len <= ESP_NOW_MAX_DATA_LEN);
  esp_err_t res = ESP_OK;
  if ((res = esp_now_send(mac_addr, data, len)) != ESP_OK) {
    LOGE(TAG, "Error sending ESP-NOW data (%d)", res);
    return false;
  }

  return true;
}

bool remote_wrapper_broadcast_fragmented_message(uint8_t sub_type, const uint8_t *data, size_t len) {
    return remote_wrapper_send_fragmented_message(broadcast_mac, sub_type, data, len);
}


bool remote_wrapper_send_fragmented_message(const uint8_t *mac_addr, uint8_t sub_type, const uint8_t *data, size_t len) {
    static uint16_t seq_id_counter = 0;
    const size_t max_payload_per_fragment = REMOTE_COMMS_DATA_MAX - BINARY_FRAGMENT_MSG_HEADER_SIZE;

    if (max_payload_per_fragment <= 0) {
        LOGE(TAG, "Cannot send fragmented message, max payload size is too small.");
        return false;
    }

    uint16_t total_fragments = (len + max_payload_per_fragment - 1) / max_payload_per_fragment;
    uint16_t current_seq_id = seq_id_counter++;

    LOGI(TAG, "Sending fragmented message: seq=%u, total_size=%zu, fragments=%u to " MACSTR,
         current_seq_id, len, total_fragments, MAC2STR(mac_addr));

    for (uint16_t i = 0; i < total_fragments; i++) {
        size_t offset = i * max_payload_per_fragment;
        size_t fragment_len = (i == total_fragments - 1) ? (len - offset) : max_payload_per_fragment;

        size_t total_msg_len = BINARY_FRAGMENT_MSG_HEADER_SIZE + fragment_len;
        
        // Optimize: Use stack buffer instead of malloc to prevent heap fragmentation
        uint8_t buf[ESP_NOW_MAX_DATA_LEN];
        binary_fragment_msg_t *fragment_msg = (binary_fragment_msg_t *)buf;

        fragment_msg->type = MSG_TYPE_BINARY;
        fragment_msg->sub_type = sub_type;
        fragment_msg->seq_id = current_seq_id;
        fragment_msg->total_payload_size = len;
        fragment_msg->total_fragments = total_fragments;
        fragment_msg->fragment_index = i;
        fragment_msg->fragment_offset = offset;
        fragment_msg->fragment_len = fragment_len;
        memcpy(fragment_msg->data, data + offset, fragment_len);

        if (!remote_wrapper_send(mac_addr, (const uint8_t*)fragment_msg, total_msg_len)) {
            LOGW(TAG, "Failed to send fragment %u of seq %u", i, current_seq_id);
            // We could attempt retries here, but for now we fail fast.
            return false;
        }
    }
    return true;
}

void remote_wrapper_deinit() {
  esp_now_deinit();
  // esp_wifi_stop();  // you may want to keep wifi running for other tasks.
}

// Static callback function for data sent
static void on_data_sent(const uint8_t *mac_addr,
                         esp_now_send_status_t status) {
  if (g_send_cb) {
    g_send_cb(mac_addr, status, g_user_data);
  }
}

// Static callback function for data received
static void on_data_recv(const esp_now_recv_info_t *esp_now_info,
                         const uint8_t *data, int data_len) {
  if (g_recv_cb) {
    // Note: In ESP-IDF 5+, esp_now_info->src_addr is directly available.
    g_recv_cb(esp_now_info->src_addr, data, data_len, g_user_data);
  }
}

#elif defined(ESP32P4_HW) && defined(REMOTE_COMMS_C6_BRIDGE)

// =============================================================================
// ESP32-P4 + ESP32-C6 bridge implementation
//
// The C6 sidecar chip runs the companion firmware from c6_espnow_bridge/ and
// communicates with the P4 over a dedicated UART using a simple binary framing
// protocol (bridge_protocol.h).  From the app's perspective the API is
// identical to the native ESP-NOW path above.
// =============================================================================

#include <string.h>
#include "driver/uart.h"
#include "debug.h"

static const char *TAG = "remote_comms_wrapper";

// ---- Bridge protocol constants (mirrors c6_espnow_bridge/src/bridge_protocol.h) ----
#define BRIDGE_SOF0           0xAB
#define BRIDGE_SOF1           0xCD
#define BRIDGE_DIR_INCOMING   0x01   /**< C6 → P4: payload received from ESP-NOW */
#define BRIDGE_DIR_OUTGOING   0x02   /**< P4 → C6: payload to send via ESP-NOW   */
/** Debug / hello frames — valid framing, but not dispatched to recv_cb.     */
#define BRIDGE_DIR_DEBUG      0x44   /**< 'D': human-readable info frame        */
#define BRIDGE_PROTOCOL_VERSION 1
/** Typing this byte while parser is idle triggers a plain-text info dump.   */
#define BRIDGE_DEBUG_TRIGGER  0x3F   /**< '?'                                   */
#define BRIDGE_MAC_LEN        6
#define BRIDGE_MAX_PAYLOAD    250
#define BRIDGE_FRAME_OVERHEAD 12     /**< SOF(2)+DIR(1)+MAC(6)+LEN(2)+CRC(1)   */
#define BRIDGE_MAX_FRAME_SIZE (BRIDGE_FRAME_OVERHEAD + BRIDGE_MAX_PAYLOAD)

// ---- UART pin / port defaults (override via platformio build_flags) ----------
#ifndef C6_BRIDGE_UART_NUM
#define C6_BRIDGE_UART_NUM  1
#endif
#ifndef C6_BRIDGE_UART_TX
#define C6_BRIDGE_UART_TX   4    /**< P4 GPIO → C6 RX pin */
#endif
#ifndef C6_BRIDGE_UART_RX
#define C6_BRIDGE_UART_RX   5    /**< P4 GPIO ← C6 TX pin */
#endif
#ifndef C6_BRIDGE_UART_BAUD
#define C6_BRIDGE_UART_BAUD 921600
#endif

#define C6_BRIDGE_RX_BUF_SIZE 2048
#define C6_BRIDGE_TX_BUF_SIZE 1024

static const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static remote_wrapper_recv_cb_t g_recv_cb   = NULL;
static remote_wrapper_send_cb_t g_send_cb   = NULL;
static void                    *g_user_data = NULL;
static TaskHandle_t             g_rx_task   = NULL;

// ---- CRC8 (plain XOR over MAC + LEN + payload) ------------------------------
static uint8_t bridge_crc8(const uint8_t *mac, uint16_t len,
                             const uint8_t *data)
{
    uint8_t crc = 0;
    for (int i = 0; i < BRIDGE_MAC_LEN; i++) crc ^= mac[i];
    crc ^= (uint8_t)(len & 0xFF);
    crc ^= (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

// ---- Frame encoder ----------------------------------------------------------
static size_t bridge_encode_frame(uint8_t *out, uint8_t dir,
                                   const uint8_t *mac,
                                   const uint8_t *payload, uint16_t len)
{
    size_t i = 0;
    out[i++] = BRIDGE_SOF0;
    out[i++] = BRIDGE_SOF1;
    out[i++] = dir;
    memcpy(&out[i], mac, BRIDGE_MAC_LEN); i += BRIDGE_MAC_LEN;
    out[i++] = (uint8_t)(len & 0xFF);
    out[i++] = (uint8_t)(len >> 8);
    memcpy(&out[i], payload, len); i += len;
    out[i++] = bridge_crc8(mac, len, payload);
    return i;
}

// ---- Debug / hello helpers --------------------------------------------------

/**
 * Build a human-readable info string into @p buf.  Returns the length.
 * Kept under BRIDGE_MAX_PAYLOAD bytes so it fits in one debug frame.
 */
static size_t bridge_build_hello(char *buf, size_t buf_size)
{
    return (size_t)snprintf(buf, buf_size,
        "\r\n"
        "=== P4 C6-Bridge Host ===\r\n"
        "Protocol version : %d\r\n"
        "Build            : " __DATE__ " " __TIME__ "\r\n"
        "Bridge UART      : UART%d, TX=GPIO%d, RX=GPIO%d, %d baud\r\n"
        "Send '?' on bridge wire for this info again\r\n"
        "=========================",
        BRIDGE_PROTOCOL_VERSION,
        C6_BRIDGE_UART_NUM, C6_BRIDGE_UART_TX,
        C6_BRIDGE_UART_RX, C6_BRIDGE_UART_BAUD);
}

/**
 * Send plain-text hello on the bridge UART.  The C6's frame parser discards
 * non-0xAB bytes while idle, so plain ASCII causes no framing noise.
 * Used both at startup and in response to a '?' probe from a human.
 */
static void bridge_send_hello_plaintext(void)
{
    char   buf[BRIDGE_MAX_PAYLOAD];
    size_t len = bridge_build_hello(buf, sizeof(buf));
    if (len < sizeof(buf) - 3) {
        buf[len++] = '\r';
        buf[len++] = '\n';
        buf[len]   = '\0';
    }
    uart_write_bytes(C6_BRIDGE_UART_NUM, buf, len);
    LOGI(TAG, "%s", buf);
}

// ---- Receive task: byte-stream → frame parser → recv_cb --------------------
typedef enum {
    RX_SOF0, RX_SOF1, RX_DIR,
    RX_MAC, RX_LEN_LO, RX_LEN_HI,
    RX_DATA, RX_CRC,
} rx_state_t;

static void c6_bridge_rx_task(void *arg)
{
    (void)arg;
    rx_state_t state       = RX_SOF0;
    uint8_t    dir         = 0;
    uint8_t    mac[BRIDGE_MAC_LEN];
    uint8_t    mac_pos     = 0;
    uint16_t   payload_len = 0;
    uint16_t   data_pos    = 0;
    uint8_t    data_buf[BRIDGE_MAX_PAYLOAD];

    uint8_t b;
    while (true) {
        if (uart_read_bytes(C6_BRIDGE_UART_NUM, &b, 1,
                            pdMS_TO_TICKS(20)) <= 0) {
            continue;
        }

        switch (state) {
            case RX_SOF0:
                if (b == BRIDGE_SOF0) {
                    state = RX_SOF1;
                } else if (b == BRIDGE_DEBUG_TRIGGER) {
                    // '?' typed on the bridge wire by a human — respond with
                    // plain text.  The C6's parser discards non-0xAB bytes.
                    bridge_send_hello_plaintext();
                }
                // All other bytes while idle are silently discarded.
                break;
            case RX_SOF1:
                state = (b == BRIDGE_SOF1) ? RX_DIR : RX_SOF0;
                break;
            case RX_DIR:
                dir     = b;
                mac_pos = 0;
                state   = RX_MAC;
                break;
            case RX_MAC:
                mac[mac_pos++] = b;
                if (mac_pos == BRIDGE_MAC_LEN) state = RX_LEN_LO;
                break;
            case RX_LEN_LO:
                payload_len = b;
                state       = RX_LEN_HI;
                break;
            case RX_LEN_HI:
                payload_len |= ((uint16_t)b << 8);
                if (payload_len == 0 || payload_len > BRIDGE_MAX_PAYLOAD) {
                    LOGW(TAG, "Bridge RX: bad length %u — resyncing",
                         payload_len);
                    state = RX_SOF0;
                } else {
                    data_pos = 0;
                    state    = RX_DATA;
                }
                break;
            case RX_DATA:
                data_buf[data_pos++] = b;
                if (data_pos == payload_len) state = RX_CRC;
                break;
            case RX_CRC: {
                uint8_t expected = bridge_crc8(mac, payload_len, data_buf);
                if (b == expected) {
                    if (dir == BRIDGE_DIR_INCOMING && g_recv_cb) {
                        g_recv_cb(mac, data_buf, (int)payload_len,
                                  g_user_data);
                    } else if (dir == BRIDGE_DIR_DEBUG) {
                        // Human-readable hello / info frame from C6.
                        // Log it and discard — do NOT call recv_cb.
                        uint16_t safe_len = payload_len < BRIDGE_MAX_PAYLOAD
                                           ? payload_len
                                           : BRIDGE_MAX_PAYLOAD - 1;
                        data_buf[safe_len] = '\0';
                        LOGI(TAG, "Debug from C6: %s", (char *)data_buf);
                    }
                    // DIR values other than INCOMING / DEBUG are silently
                    // ignored (e.g. a looped-back OUTGOING frame).
                } else {
                    LOGW(TAG, "Bridge RX: CRC mismatch (got 0x%02X exp 0x%02X)",
                         b, expected);
                }
                state = RX_SOF0;
                break;
            }
        }
    }
}

// ---- Public API -------------------------------------------------------------

bool remote_wrapper_init(remote_wrapper_recv_cb_t recv_cb,
                         remote_wrapper_send_cb_t send_cb, void *user_data)
{
    g_recv_cb   = recv_cb;
    g_send_cb   = send_cb;
    g_user_data = user_data;

    const uart_config_t cfg = {
        .baud_rate           = C6_BRIDGE_UART_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(C6_BRIDGE_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(C6_BRIDGE_UART_NUM,
                                  C6_BRIDGE_UART_TX, C6_BRIDGE_UART_RX,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(C6_BRIDGE_UART_NUM,
                                        C6_BRIDGE_RX_BUF_SIZE,
                                        C6_BRIDGE_TX_BUF_SIZE,
                                        0, NULL, 0));

    LOGI(TAG, "C6 bridge UART%d: TX=GPIO%d RX=GPIO%d @%d baud",
         C6_BRIDGE_UART_NUM, C6_BRIDGE_UART_TX,
         C6_BRIDGE_UART_RX, C6_BRIDGE_UART_BAUD);

    BaseType_t ok = xTaskCreatePinnedToCore(
        c6_bridge_rx_task,
        "c6_bridge_rx",
        1024 * 4,
        NULL,
        tskIDLE_PRIORITY + 2,
        &g_rx_task,
        TASK_MACHINE_STATE_PROC_CORE);

    if (ok != pdPASS) {
        LOGE(TAG, "Failed to start C6 bridge RX task");
        return false;
    }

    LOGI(TAG, "C6 ESP-NOW bridge ready");

    // Send plain-text hello on the bridge UART at boot so a human monitoring
    // the wire-jumped UART on either end can confirm it is correctly configured
    // and wired.  The C6's frame parser silently discards non-0xAB bytes.
    bridge_send_hello_plaintext();

    return true;
}

bool remote_wrapper_add_peer(const uint8_t *mac_addr)
{
    // Peer management is handled transparently by the C6 bridge firmware.
    LOGI(TAG, "add_peer " MACSTR " (delegated to C6)", MAC2STR(mac_addr));
    return true;
}

bool remote_wrapper_add_peer_if_not_known(const uint8_t *received_mac_addr,
                                          uint8_t *stored_mac_addr)
{
    bool is_uninit = (memcmp(stored_mac_addr, "\0\0\0\0\0\0", 6) == 0) ||
                     (memcmp(stored_mac_addr, broadcast_mac, 6) != 0);

    LOGI(TAG,
         "RECV hub MAC " MACSTR " == new MAC " MACSTR " => is_unknown %d",
         MAC2STR(stored_mac_addr), MAC2STR(received_mac_addr), is_uninit);

    if (is_uninit || memcmp(received_mac_addr, stored_mac_addr, 6) == 0) {
        if (is_uninit) {
            memcpy(stored_mac_addr, received_mac_addr, 6);
            LOGI(TAG, "Learned hub MAC: " MACSTR, MAC2STR(stored_mac_addr));
        }
        return true;
    }
    LOGI(TAG,
         "Received broadcast from a different hub! Stored: " MACSTR
         ", Received: " MACSTR,
         MAC2STR(stored_mac_addr), MAC2STR(received_mac_addr));
    return false;
}

bool remote_wrapper_send_now(const uint8_t *mac_addr, const uint8_t *data,
                              size_t len)
{
    if (len > BRIDGE_MAX_PAYLOAD) {
        LOGE(TAG, "send_now: payload %zu > max %d", len, BRIDGE_MAX_PAYLOAD);
        return false;
    }
    uint8_t frame[BRIDGE_MAX_FRAME_SIZE];
    size_t  frame_len = bridge_encode_frame(frame, BRIDGE_DIR_OUTGOING,
                                             mac_addr, data, (uint16_t)len);
    int written = uart_write_bytes(C6_BRIDGE_UART_NUM,
                                   (const char *)frame, (size_t)frame_len);
    if (written != (int)frame_len) {
        LOGE(TAG, "send_now: wrote %d/%zu bytes", written, frame_len);
        return false;
    }
    LOGV(TAG, "send_now: %zu payload bytes → %zu frame bytes", len, frame_len);
    return true;
}

bool remote_wrapper_send(const uint8_t *mac_addr, const uint8_t *data,
                          size_t len)
{
    // UART writes are synchronous and fast; no separate send queue needed.
    return remote_wrapper_send_now(mac_addr, data, len);
}

bool remote_wrapper_send_fragmented_message(const uint8_t *mac_addr,
                                             uint8_t sub_type,
                                             const uint8_t *data, size_t len)
{
    static uint16_t seq_id_counter = 0;
    const size_t max_payload_per_fragment =
        REMOTE_COMMS_DATA_MAX - BINARY_FRAGMENT_MSG_HEADER_SIZE;

    if (max_payload_per_fragment == 0) {
        LOGE(TAG, "Cannot send fragmented message: max payload too small");
        return false;
    }

    uint16_t total_fragments =
        (uint16_t)((len + max_payload_per_fragment - 1) /
                   max_payload_per_fragment);
    uint16_t current_seq_id = seq_id_counter++;

    LOGI(TAG,
         "Sending fragmented message: seq=%u, total_size=%zu, "
         "fragments=%u to " MACSTR,
         current_seq_id, len, total_fragments, MAC2STR(mac_addr));

    for (uint16_t i = 0; i < total_fragments; i++) {
        size_t offset       = i * max_payload_per_fragment;
        size_t fragment_len = (i == total_fragments - 1)
                                  ? (len - offset)
                                  : max_payload_per_fragment;
        size_t total_msg_len =
            BINARY_FRAGMENT_MSG_HEADER_SIZE + fragment_len;

        uint8_t buf[BRIDGE_MAX_PAYLOAD];
        binary_fragment_msg_t *fragment_msg = (binary_fragment_msg_t *)buf;

        fragment_msg->type               = MSG_TYPE_BINARY;
        fragment_msg->sub_type           = sub_type;
        fragment_msg->seq_id             = current_seq_id;
        fragment_msg->total_payload_size = (uint32_t)len;
        fragment_msg->total_fragments    = total_fragments;
        fragment_msg->fragment_index     = i;
        fragment_msg->fragment_offset    = (uint32_t)offset;
        fragment_msg->fragment_len       = (uint16_t)fragment_len;
        memcpy(fragment_msg->data, data + offset, fragment_len);

        if (!remote_wrapper_send(mac_addr, (const uint8_t *)fragment_msg,
                                  total_msg_len)) {
            LOGW(TAG, "Failed to send fragment %u of seq %u", i,
                 current_seq_id);
            return false;
        }
    }
    return true;
}

bool remote_wrapper_broadcast_fragmented_message(uint8_t sub_type,
                                                  const uint8_t *data,
                                                  size_t len)
{
    return remote_wrapper_send_fragmented_message(broadcast_mac, sub_type,
                                                   data, len);
}

void remote_wrapper_deinit()
{
    if (g_rx_task) {
        vTaskDelete(g_rx_task);
        g_rx_task = NULL;
    }
    uart_driver_delete(C6_BRIDGE_UART_NUM);
}

#else
bool remote_wrapper_init(remote_wrapper_recv_cb_t recv_cb,
                         remote_wrapper_send_cb_t send_cb, void *user_data) {
  return true;
}
bool remote_wrapper_add_peer(const uint8_t *mac_addr) { return true; }

bool remote_wrapper_add_peer_if_not_known(const uint8_t *received_mac_addr,
                                          uint8_t *stored_mac_addr) {
  return true;
}

bool remote_wrapper_send(const uint8_t *mac_addr, const uint8_t *data,
                         size_t len) {
  return true;
}

bool remote_wrapper_send_now(const uint8_t *mac_addr, const uint8_t *data,
                             size_t len) {
  return true;
}

bool remote_wrapper_send_fragmented_message(const uint8_t *mac_addr, uint8_t sub_type, const uint8_t *data, size_t len) {
    // Dummy implementation for non-ESP32 platforms
    return true;
}

bool remote_wrapper_broadcast_fragmented_message(uint8_t sub_type, const uint8_t *data, size_t len) {
    // Dummy implementation for non-ESP32 platforms
    return true;
}

void remote_wrapper_deinit() {}

#endif
