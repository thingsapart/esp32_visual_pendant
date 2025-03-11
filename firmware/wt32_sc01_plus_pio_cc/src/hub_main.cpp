#ifdef ESP_NOW_HUB

#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "driver/driver_interface.hpp"
#include "driver/remote_comms_wrapper.h"

#include "machine/machine_rrf.h"
#include "machine/machine_interface.h"

#include "debug.h"

static const char *TAG = "hub_main";

// --- Configuration ---
#define HUB_POLL_INTERVAL_MS 200
#define FULL_STATE_INTERVAL 10  // Send full state every 10th poll

// Replace with the display's MAC address
static const uint8_t display_mac_address[] = DISPLAY_MAC_ADDR;

// --- Global Variables ---
machine_rrf_t *g_machine = NULL;  // Global pointer to the machine interface
int g_full_state_counter = 0;

// --- ESP-NOW Message Types ---

// --- Callback Functions (for machine interface) ---
// These are called when the machine's state changes.

void on_machine_state_change(machine_interface_t *machine, void *user_data) {
    // Send status update message
    status_msg_t msg;
    msg.type = MSG_TYPE_STATUS;
    msg.status = machine->machine_status;
    remote_wrapper_send(display_mac_address, (uint8_t*)&msg, sizeof(msg));
    LOGI(TAG, "Sending status %d", machine->machine_status);
}

void on_position_change(machine_interface_t *machine, void *user_data) {
     position_msg_t msg;
    msg.type = MSG_TYPE_POSITION;
    msg.x = machine->position[0];
    msg.y = machine->position[1];
    msg.z = machine->position[2];
    msg.wcs_x = machine->wcs_position[0];
    msg.wcs_y = machine->wcs_position[1];
    msg.wcs_z = machine->wcs_position[2];
    LOGI(TAG, "Sending position %f, %f, %f (%f, %f, %f)", 
        machine->position[0], machine->position[1], machine->position[2],
         machine->wcs_position[0],  machine->wcs_position[1],  machine->wcs_position[2]);
    remote_wrapper_send(display_mac_address, (uint8_t*)&msg, sizeof(msg));
}

void on_home_change(machine_interface_t *machine, void *user_data) {
    // Send homing status update
    homed_msg_t msg;
    msg.type = MSG_TYPE_HOMED;
    msg.x_homed = machine->axes_homed[0];
    msg.y_homed = machine->axes_homed[1];
    msg.z_homed = machine->axes_homed[2];
    LOGI(TAG, "Sending axes homed %d, %d, %d", machine->axes_homed[0], machine->axes_homed[1], machine->axes_homed[2]);
    remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg));
}

void on_wcs_change(machine_interface_t *machine, void *user_data) {
    // Send WCS update
    wcs_msg_t msg;
    msg.type = MSG_TYPE_WCS;
    msg.wcs = machine->wcs;
    remote_wrapper_send(display_mac_address, (uint8_t*)&msg, sizeof(msg));
    LOGI(TAG, "Sending wcs changed: %d", machine->wcs);
}

void on_feed_change(machine_interface_t *machine, void *user_data) {
     feed_msg_t msg;
     msg.type = MSG_TYPE_FEED;
     msg.feed = machine->feed;
     msg.feed_req = machine->feed_req;
     msg.feed_multiplier = machine->feed_multiplier;
     remote_wrapper_send(display_mac_address, (uint8_t*)&msg, sizeof(msg));
    LOGI(TAG, "Sending feed changed: %f/%f (x%f)", machine->feed, machine->feed_req, machine->feed_multiplier);
}

void on_sensors_change(machine_interface_t *machine, void *user_data) {
    // TODO
}

void on_dialogs_change(machine_interface_t *machine, void *user_data) {
    // TODO
}

void on_spindles_tools_change(machine_interface_t *machine, void *user_data) {
    spindles_tools_msg_t msg;
    msg.type = MSG_TYPE_SPINDLES_TOOLS;
    msg.rpm = machine->spindles->rpm;
    msg.tool = ""; //machine->tool;
    // we should not send string data directly over the air, it's inefficient.
    // so we're sending only the length, for this example.
    remote_wrapper_send(display_mac_address, (uint8_t *)&msg, sizeof(msg.type) + sizeof(msg.rpm) + sizeof(size_t) /* tool len */);
    LOGI(TAG, "Sending tool/spindle changed: rpm %d / %s", machine->spindles->rpm, msg.tool);
}

void on_connected_change(machine_interface_t *machine, void *user_data) {
     _df(0, "[%s] Machine connection state changed: %s", TAG, machine->is_connected(machine) ? "connected" : "disconnected");
}

// Add more callback functions for other state changes...

// --- ESP-NOW Callbacks ---

void on_remote_data_sent(const uint8_t *mac_addr, int status, void *user_data) {
    _df(0, "[%s] ESP-NOW send status: %s", TAG, status == 0 ? "success" : "fail");
}

void on_remote_data_recv(const uint8_t *mac_addr, const uint8_t *data, int data_len, void *user_data) {
    // We don't expect to receive any data on the hub, but you might implement
    // a two-way communication system later.
     _df(0, "[%s] Received ESP-NOW data from " MACSTR ", len: %d", TAG, MAC2STR(mac_addr), data_len);
}

// --- Main Task ---

void hub_task(void *pvParameters) {
    // Initialize ESP-NOW
    if (!remote_wrapper_init(on_remote_data_recv, on_remote_data_sent, NULL)) {
        _df(2, "[%s] Failed to initialize ESP-NOW", TAG);
        vTaskDelete(NULL);
        return;
    }

    // Add the display as a peer
    if (!remote_wrapper_add_peer(display_mac_address)) {
        _df(2, "[%s] Failed to add display as peer", TAG);
        vTaskDelete(NULL);
        return;
    }

      // Initialize the machine interface
    g_machine = machine_rrf_create(0, HUB_POLL_INTERVAL_MS, MACH_UART_PIN_TX, MACH_UART_PIN_RX);  // Use UART 0
    if (!g_machine) {
        _df(2, "[%s] Failed to create machine interface", TAG);
        vTaskDelete(NULL);
        return;
    }

    // Register callbacks
    machine_interface_add_state_change_cb(&g_machine->base, NULL, on_machine_state_change);
    machine_interface_add_pos_changed_cb(&g_machine->base, NULL, on_position_change);
    machine_interface_add_home_changed_cb(&g_machine->base, NULL, on_home_change);
    machine_interface_add_wcs_changed_cb(&g_machine->base, NULL, on_wcs_change);
    machine_interface_add_feed_changed_cb(&g_machine->base, NULL, on_feed_change);
    machine_interface_add_sensors_changed_cb(&g_machine->base, NULL, on_sensors_change);
    machine_interface_add_dialogs_changed_cb(&g_machine->base, NULL, on_dialogs_change);
    machine_interface_add_spindles_tools_changed_cb(&g_machine->base, NULL, on_spindles_tools_change);
    machine_interface_add_connected_changed_cb(&g_machine->base, NULL, on_connected_change);

    // Main loop
    while (1) {
        // Poll the machine
        machine_interface_task_loop_iter(&g_machine->base);

         // Send keep-alive message
        keep_alive_msg_t keep_alive_msg;
        keep_alive_msg.type = MSG_TYPE_KEEP_ALIVE;
        remote_wrapper_send(display_mac_address, (uint8_t*)&keep_alive_msg, sizeof(keep_alive_msg));

        _df(2, "[%s] Tick...", TAG);

        vTaskDelay(pdMS_TO_TICKS(HUB_POLL_INTERVAL_MS));
    }
}


void app_main() {
}

void setupTasl() {
   xTaskCreate(hub_task, "hub_task", 4096, NULL, 5, NULL);
}

void setup() {
    mcu_setup();
    mcu_startup();

    // Initialize ESP-NOW
    if (!remote_wrapper_init(on_remote_data_recv, on_remote_data_sent, NULL)) {
        _df(2, "[%s] Failed to initialize ESP-NOW", TAG);
        return;
    }

    // Add the display as a peer
    if (!remote_wrapper_add_peer(display_mac_address)) {
        _df(2, "[%s] Failed to add display as peer", TAG);
        return;
    }

    // Initialize the machine interface
    g_machine = machine_rrf_create(0, HUB_POLL_INTERVAL_MS, MACH_UART_PIN_TX, MACH_UART_PIN_RX);  // Use UART 0
    if (!g_machine) {
        _df(2, "[%s] Failed to create machine interface", TAG);
        return;
    }

    // Register callbacks
    machine_interface_add_state_change_cb(&g_machine->base, NULL, on_machine_state_change);
    machine_interface_add_pos_changed_cb(&g_machine->base, NULL, on_position_change);
    machine_interface_add_home_changed_cb(&g_machine->base, NULL, on_home_change);
    machine_interface_add_wcs_changed_cb(&g_machine->base, NULL, on_wcs_change);
    machine_interface_add_feed_changed_cb(&g_machine->base, NULL, on_feed_change);
    machine_interface_add_sensors_changed_cb(&g_machine->base, NULL, on_sensors_change);
    machine_interface_add_dialogs_changed_cb(&g_machine->base, NULL, on_dialogs_change);
    machine_interface_add_spindles_tools_changed_cb(&g_machine->base, NULL, on_spindles_tools_change);
    machine_interface_add_connected_changed_cb(&g_machine->base, NULL, on_connected_change);

    vTaskDelay(pdMS_TO_TICKS(HUB_POLL_INTERVAL_MS));
}

unsigned int ctr = 0;
void loop() {
    // Poll the machine
    machine_interface_task_loop_iter(&g_machine->base);

    const unsigned int iter = ctr++ % 10;
    if (iter  == 1) {
        on_machine_state_change(&g_machine->base, NULL);
    } else if (iter == 2) {
        on_position_change(&g_machine->base, NULL);
    } else if (iter == 3) {
        on_home_change(&g_machine->base, NULL);
    } else if (iter == 4) {
        on_wcs_change(&g_machine->base, NULL);
    } else if (iter == 5) {
        on_feed_change(&g_machine->base, NULL);
    } else if (iter == 6) {
        on_sensors_change(&g_machine->base, NULL);
    } else if (iter == 7) {
        on_dialogs_change(&g_machine->base, NULL);
    } else if (iter == 8) {
        on_spindles_tools_change(&g_machine->base, NULL);
    } else {
        // Send keep-alive message
        keep_alive_msg_t keep_alive_msg;
        keep_alive_msg.type = MSG_TYPE_KEEP_ALIVE;
        remote_wrapper_send(display_mac_address, (uint8_t*)&keep_alive_msg, sizeof(keep_alive_msg));
    }

    _df(2, "[%s] Tick...", TAG);

    delay(HUB_POLL_INTERVAL_MS - 10);
}
#endif