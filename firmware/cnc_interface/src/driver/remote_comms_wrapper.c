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
#if defined(ESP_IDF_LEGACY)
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);
#else
static void on_data_sent(const wifi_tx_info_t *, esp_now_send_status_t status);
#endif
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

#if defined(ESP_IDF_LEGACY)
static void on_data_sent(const uint8_t *mac_addr,
                         esp_now_send_status_t status) {
  if (g_send_cb) {
    g_send_cb(mac_addr, status, g_user_data);
  }
}
#else
static void on_data_sent(const wifi_tx_info_t *info,
                         esp_now_send_status_t status) {
  if (g_send_cb) {
    //g_send_cb(mac_addr, status, g_user_data);
    g_send_cb(info ? info->des_addr : NULL, status, g_user_data);
  }
}
#endif

// Static callback function for data received
static void on_data_recv(const esp_now_recv_info_t *esp_now_info,
                         const uint8_t *data, int data_len) {
  if (g_recv_cb) {
    // Note: In ESP-IDF 5+, esp_now_info->src_addr is directly available.
    g_recv_cb(esp_now_info->src_addr, data, data_len, g_user_data);
  }
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
