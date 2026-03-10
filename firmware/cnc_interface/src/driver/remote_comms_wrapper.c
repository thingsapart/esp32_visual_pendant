// remote_comms_wrapper.c

#include "remote_comms_wrapper.h"
#include "debug.h"

// ---- Shared Stats ----
struct remote_wrapper_stats_t {
    uint32_t rx_total;
    uint32_t rx_bridge_crc_err;
    uint32_t rx_err;
    uint32_t tx_total;
    uint32_t tx_failed;
    uint32_t bridge_timeouts;
};
static struct remote_wrapper_stats_t s_remote_stats = {0};

void remote_wrapper_print_stats(void) {
    LOGI("ESP-NOW", "Stats - RX: %u, TX: %u, TX Err: %u, CRC Err: %u, RX Err: %u, TO: %u",
           (unsigned)s_remote_stats.rx_total,
           (unsigned)s_remote_stats.tx_total,
           (unsigned)s_remote_stats.tx_failed,
           (unsigned)s_remote_stats.rx_bridge_crc_err,
           (unsigned)s_remote_stats.rx_err,
           (unsigned)s_remote_stats.bridge_timeouts);
}

#if defined(ESP32_HW)

#include <assert.h>
#include <esp_event.h>
#include <esp_netif.h>
#ifdef REMOTE_COMMS_C6_SDIO_BRIDGE
#include "esp_bridged_esp_now.h"
#else
#include <esp_now.h>
#include <esp_wifi.h>
#endif
#include <nvs_flash.h>
#include <string.h>

#include "debug.h"
#include "driver/task_registry.h"

static const char *TAG = "remote_comms_wrapper";

static const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Forward declarations of static callback functions
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);
static void on_data_recv(const esp_now_recv_info_t *esp_now_info,
                         const uint8_t *data, int data_len);

// Store callback functions
static remote_wrapper_send_cb_t g_send_cb   = NULL;
static void                    *g_send_user_data = NULL;

// Multi-receiver table
typedef struct { remote_wrapper_recv_cb_t cb; void *user; } rcb_slot_t;
static rcb_slot_t g_recv_cbs[REMOTE_WRAPPER_MAX_RECV_CBS];

#ifndef REMOTE_COMMS_C6_SDIO_BRIDGE

static void wifi_init() {
  esp_err_t _err;
  ESP_ERROR_CHECK(esp_netif_init());
  _err = esp_event_loop_create_default();
  if (_err != ESP_OK && _err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(_err);
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

#else

// C6-Bridge because we have no wifi! C6 running custom firmware so built-in
// Wifi won't start.
static void wifi_init() {}

#endif

bool remote_wrapper_add_recv_cb(remote_wrapper_recv_cb_t recv_cb, void *user_data)
{
    if (!recv_cb) return false;
    for (int i = 0; i < REMOTE_WRAPPER_MAX_RECV_CBS; i++) {
        if (g_recv_cbs[i].cb == recv_cb) return true;  // already registered
        if (!g_recv_cbs[i].cb) {
            g_recv_cbs[i].cb   = recv_cb;
            g_recv_cbs[i].user = user_data;
            return true;
        }
    }
    LOGE(TAG, "remote_wrapper_add_recv_cb: table full");
    return false;
}

void remote_wrapper_remove_recv_cb(remote_wrapper_recv_cb_t recv_cb)
{
    for (int i = 0; i < REMOTE_WRAPPER_MAX_RECV_CBS; i++) {
        if (g_recv_cbs[i].cb == recv_cb) {
            g_recv_cbs[i].cb   = NULL;
            g_recv_cbs[i].user = NULL;
            return;
        }
    }
}

static void dispatch_recv(const uint8_t *mac, const uint8_t *data, int len)
{
    for (int i = 0; i < REMOTE_WRAPPER_MAX_RECV_CBS; i++) {
        if (g_recv_cbs[i].cb)
            g_recv_cbs[i].cb(mac, data, len, g_recv_cbs[i].user);
    }
}


#define USE_SEND_QUEUE

#ifdef USE_SEND_QUEUE
#define QUEUE_LENGTH 16
// Priority +3: below MachineRemoteProc (+4) so a fresh state response is
// always parsed before the next outgoing packet is dispatched; above lvgl_task
// (+2) so display rendering never delays an ESP-NOW transmission.
// Order: MachineSendTask(+5) > MachineRemoteProc(+4) > remote_send_task(+3)
//        > lvgl_task(+2) > bridged_sdio_rx(+2) > Machine/etc(+1)
#define TASK_PRIORITY (tskIDLE_PRIORITY + 3)

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
        LOGV(TAG, "Sent remote message len %d [OK]", item.data_len);
        LOGD(TAG, "[->%d]", item.data_len);
      } else {
        LOGW(TAG, "Failed to send remote message len %d", item.data_len);
      }

      // 1 ms inter-packet gap is sufficient to let the ESP-NOW driver release
      // its internal TX buffer; the original 5 ms was unnecessarily generous.
      vTaskDelay(1 / portTICK_PERIOD_MS);
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

    task_registry_register_handle(task_handle, "remote_send_task");

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

  BaseType_t res = xQueueSend(remote_send_queue, &item, pdMS_TO_TICKS(200));
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
#ifndef REMOTE_COMMS_C6_SDIO_BRIDGE
  // Initialize NVS (needed for Wi-Fi) - not done when we use c6_espnow_bridge.
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  wifi_init();
#endif

  // Initialize ESP-NOW
  // WiFi STA must already be running (started by mcu_startup() and kept alive
  // for ESP_NOW_HUB builds). esp_now_send() returns ESP_ERR_ESPNOW_IF if the
  // Arduino-managed STA netif is absent.
  ESP_ERROR_CHECK(esp_now_init());

  g_send_cb        = send_cb;
  g_send_user_data = user_data;
  remote_wrapper_add_recv_cb(recv_cb, user_data);

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
  esp_err_t res = esp_now_add_peer(&peer_info);
  if (res == ESP_OK || res == ESP_ERR_ESPNOW_EXIST) {
    // ESP_ERR_ESPNOW_EXIST is not an error: cam_transport_espnow may have
    // already added the peer in its auto-discovery recv callback before this
    // function is called.  Treat it as success.
    return true;
  }
  LOGE(TAG, "Failed to add peer %d", res);
  return false;
}

bool remote_wrapper_add_peer_if_not_known(const uint8_t *received_mac_addr,
                                          uint8_t *stored_mac_addr) {
  // Check if the stored MAC address is all zeros (uninitialized) or broadcast
  bool is_uninitialized = memcmp(stored_mac_addr, "\0\0\0\0\0\0", 6) == 0 ||
                          memcmp(stored_mac_addr, broadcast_mac, 6) == 0;
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
    const char *ename = esp_err_to_name(res);
    LOGE(TAG, "Error sending ESP-NOW data %s (%d) to " MACSTR,
         ename, res, MAC2STR(mac_addr));
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

    LOGI(TAG, "Sending fragmented message: seq=%u, total_size=%u, fragments=%u to " MACSTR,
         current_seq_id, (unsigned)len, total_fragments, MAC2STR(mac_addr));

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
    g_send_cb(mac_addr, status, g_send_user_data);
  }
}

// Static callback function for data received
static void on_data_recv(const esp_now_recv_info_t *esp_now_info,
                         const uint8_t *data, int data_len) {
  // Note: In ESP-IDF 5+, esp_now_info->src_addr is directly available.
  dispatch_recv(esp_now_info->src_addr, data, data_len);
}

#else
bool remote_wrapper_init(remote_wrapper_recv_cb_t recv_cb,
                         remote_wrapper_send_cb_t send_cb, void *user_data) {
  (void)send_cb; (void)user_data;
  remote_wrapper_add_recv_cb(recv_cb, user_data);
  return true;
}
bool remote_wrapper_add_recv_cb(remote_wrapper_recv_cb_t recv_cb, void *user_data) {
  (void)recv_cb; (void)user_data; return true;
}
void remote_wrapper_remove_recv_cb(remote_wrapper_recv_cb_t recv_cb) {
  (void)recv_cb;
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