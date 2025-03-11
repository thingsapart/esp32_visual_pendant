// remote_wrapper.h
#ifndef REMOTE_COMMS_WRAPPER_H
#define REMOTE_COMMS_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#include "machine/machine_interface.h"

#include <esp_now.h>
#include <esp_wifi.h>


#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MSG_TYPE_KEEP_ALIVE,
    MSG_TYPE_POSITION,
    MSG_TYPE_WCS,
    MSG_TYPE_STATUS,
    MSG_TYPE_HOMED,
    MSG_TYPE_FEED,
    MSG_TYPE_SPINDLES_TOOLS,
    MSG_TYPE_SENSORS_CHANGED,
    MSG_TYPE_DIALOG_UPDATED,
} remote_message_type_t;

typedef struct {
    uint8_t type; // MSG_TYPE_KEEP_ALIVE
} keep_alive_msg_t;

typedef struct {
    uint8_t type; // MSG_TYPE_POSITION
    float x;
    float y;
    float z;
    float wcs_x;
    float wcs_y;
    float wcs_z;
} position_msg_t;

typedef struct {
    uint8_t type; // MSG_TYPE_WCS
    int wcs;
} wcs_msg_t;

typedef struct {
    uint8_t type;  // MSG_TYPE_STATUS
    machine_status_t status;
} status_msg_t;

typedef struct {
    uint8_t type;  // MSG_TYPE_HOMED;
    bool x_homed;
    bool y_homed;
    bool z_homed;
} homed_msg_t;

typedef struct {
    uint8_t type;  // MSG_TYPE_FEED
    float feed;
    float feed_req;
    float feed_multiplier;
} feed_msg_t;

typedef struct {
    uint8_t type; // MSG_TYPE_SPINDLES_TOOLS;
    int rpm;       // we only support one spindle for now
    const char *tool;  // tool name
} spindles_tools_msg_t;

typedef struct {
    uint8_t type; // MSG_TYPE_SENSORS_CHANGED,
    // TODO, add probes and endstops here.
} sensors_changed_msg_t;

typedef union {
  uint8_t type; // all structs that are part of the union MUST start with a type.
  keep_alive_msg_t keep_alive;
  position_msg_t pos;
  wcs_msg_t wcs;
  status_msg_t status;
  homed_msg_t homed;
  feed_msg_t feed;
  spindles_tools_msg_t spindles_tools;
  sensors_changed_msg_t sensors;
  // add more message types
} remote_msg_t;

typedef enum {
    CMD_TYPE_SEND_GCODE,
    CMD_TYPE_MOVE_CONT,
    CMD_TYPE_MOVE_CONT_STOP,
    CMD_TYPE_MOVE,
    CMD_TYPE_HOME_ALL,
    CMD_TYPE_HOME,
    CMD_TYPE_SET_WCS,
    CMD_TYPE_SET_WCS_ZERO,
    CMD_TYPE_NEXT_WCS,
    CMD_TYPE_RUN_MACRO,
    CMD_TYPE_START_JOB,
    CMD_TYPE_LIST_FILES,
    CMD_TYPE_PROBE,
    // Add more command types as needed
} remote_command_type_t;

typedef struct {
    uint8_t type;  // CMD_TYPE_SEND_GCODE
    uint16_t len;  // Length of the G-code string
    char gcode[];  // Flexible array member for the G-code string
} send_gcode_cmd_t;

typedef struct {
    uint8_t type;  // CMD_TYPE_MOVE_CONT
    char axis;
    float feed;
    int8_t direction; // -1, 0, or 1 (0 might not be needed)
} move_cont_cmd_t;

typedef struct {
    uint8_t type; // CMD_TYPE_MOVE_CONT_STOP
} move_cont_stop_cmd_t;

typedef struct {
    uint8_t type; // CMD_TYPE_MOVE;
    char axis;
    float feed;
    float value;
} move_cmd_t;

typedef struct {
    uint8_t type; // CMD_TYPE_HOME_ALL
} home_all_cmd_t;

typedef struct {
    uint8_t type;      // CMD_TYPE_HOME
    uint8_t axes_len; //length of axes
    char axes[]; // Flexible array member for axes
} home_cmd_t;

typedef struct {
    uint8_t type; // CMD_TYPE_SET_WCS
    int wcs;
} set_wcs_cmd_t;

typedef struct {
    uint8_t type;        // CMD_TYPE_SET_WCS_ZERO
    int wcs;
    uint8_t axes_len;   // Length of the axes string
    char axes[];        // Flexible array member for the axes string
} set_wcs_zero_cmd_t;

typedef struct{
    uint8_t type; // CMD_TYPE_NEXT_WCS
} next_wcs_cmd_t;

typedef struct {
    uint8_t type; // CMD_TYPE_PROBE
    uint16_t len;
    char gcode[];
} probe_cmd_t;

typedef struct {
  uint8_t type;  // CMD_TYPE_RUN_MACRO
  uint16_t len;  // Length of the macro name
  char macro_name[]; // Flexible array member
} run_macro_cmd_t;

typedef struct {
  uint8_t type;
  uint16_t len;  // Length of the job name
  char job_name[];  // Flexible array member
} start_job_cmd_t;

typedef struct {
  uint8_t type;
  uint16_t len;  // Length of the path
  char path[];  // Flexible array member
} list_files_cmd_t;

typedef void (*remote_wrapper_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int data_len, void *user_data);
typedef void (*remote_wrapper_send_cb_t)(const uint8_t *mac_addr, int status, void *user_data);

// typedef void (*remote_wrapper_recv_cb_t)(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
// typedef void (*remote_wrapper_send_cb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);

bool remote_wrapper_init(remote_wrapper_recv_cb_t recv_cb, remote_wrapper_send_cb_t send_cb, void *user_data);
bool remote_wrapper_add_peer(const uint8_t *mac_addr);
bool remote_wrapper_add_peer_if_not_known(const uint8_t *received_mac_addr, uint8_t *stored_mac_addr);
bool remote_wrapper_send(const uint8_t *mac_addr, const uint8_t *data, size_t len);
void remote_wrapper_deinit();

#ifdef __cplusplus
}
#endif

#endif // REMOTE_COMMS_WRAPPER_H