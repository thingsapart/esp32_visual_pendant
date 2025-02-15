#ifndef MACHINE_RRF_H
#define MACHINE_RRF_H

#include "machine_interface.h"
#include "arduino_serial_wrapper.h"

#include "cJSON.h"

#define RRF_SERIAL_UART_NUM 0
#define READ_TIMEOUT_MS 20

#ifdef __cplusplus
extern "C" {
#endif

// --- RRF-Specific Constants ---

// Mapping of RRF status codes to MachineStatus enum
// (This could also be a lookup table, but an enum is cleaner)
typedef enum {
    RRF_STATUS_C = MACHINE_STATUS_INITIALIZING,
    RRF_STATUS_F = MACHINE_STATUS_FLASHING_FIRMWARE,
    RRF_STATUS_H = MACHINE_STATUS_EMERGENCY_HALTED,
    RRF_STATUS_O = MACHINE_STATUS_OFF,
    RRF_STATUS_D = MACHINE_STATUS_PAUSED_DEC,
    RRF_STATUS_R = MACHINE_STATUS_PAUSED_RESUME,
    RRF_STATUS_S = MACHINE_STATUS_PAUSED,
    RRF_STATUS_M = MACHINE_STATUS_SIMULATING,
    RRF_STATUS_P = MACHINE_STATUS_RUNNING,
    RRF_STATUS_T = MACHINE_STATUS_TOOL_CHANGING,
    RRF_STATUS_B = MACHINE_STATUS_BUSY,
} rrf_status_t;

// --- RRF Machine Structure ---

typedef struct {
    machine_interface_t base; // Inherit from machine_interface_t

    // RRF-Specific Data
    serial_handle_t uart;
    bool connected;
    const char* input_sel;
    int input_idx;
    // Add other RRF-specific data here (e.g., network info, job details)
} machine_rrf_t;

// --- Function Prototypes ---

machine_rrf_t* machine_rrf_create(uint16_t sleep_ms, int tx_pin, int rx_pin);
machine_rrf_t* machine_rrf_init(machine_rrf_t *self, uint16_t sleep_ms, int tx_pin, int rx_pin);

void machine_rrf_destroy(machine_rrf_t *self);
void machine_rrf_task_loop_iter(machine_rrf_t *self); // Override the base class version

#ifdef __cplusplus
}
#endif

#endif // MACHINE_RRF_H













#if 0
// machine_rrf.h
#ifndef MACHINE_RRF_H
#define MACHINE_RRF_H

#include "machine_interface.h"
#include "Arduino.h" //  Arduino framework

#ifdef __cplusplus
extern "C" {
#endif

// Configuration (adjust as needed)
#define UART_BAUD_RATE 115200
#define UART_TX_PIN 43
#define UART_RX_PIN 44
#define UART_RX_BUFFER_SIZE (1024 * 16)

typedef struct {
    machine_interface_t base; // Inherit from MachineInterface
    HardwareSerial *uart;     //  HardwareSerial pointer
    bool connected;
    const char *input_sel; //  const char* for string literals
    int input_idx;
} machine_rrf_t;

// Function Prototypes
void machine_rrf_init(machine_rrf_t *machine, uint16_t sleep_ms);
void _machine_rrf_send_gcode(machine_interface_t *machine, const char *gcode);
bool _machine_rrf_has_response(machine_interface_t *machine);
const char *_machine_rrf_read_response(machine_interface_t *machine);
void _machine_rrf_update_machine_state(machine_interface_t *machine, poll_state_t poll_state);
void machine_rrf_list_files(machine_interface_t *machine, const char *path);
void machine_rrf_run_macro(machine_interface_t *machine, const char *macro_name);
void machine_rrf_start_job(machine_interface_t *machine, const char *job_name);
bool machine_rrf_is_connected(machine_interface_t *machine);
void _machine_rrf_continuous_stop(machine_interface_t *machine);
void _machine_rrf_continuous_move(machine_interface_t *machine, char axis, float feed, float direction);

// --- JSON Parsing Helpers (declarations) ---
void _machine_rrf_parse_json_response(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_m20_response(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_m409_response(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_move_axes(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_move_axes_brief(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_move_axes_ext(machine_interface_t *machine, const char *json_resp);
void _machine_rrf_parse_globals(machine_interface_t *machine, const char *json_resp);

#ifdef __cplusplus
}
#endif

#endif // MACHINE_RRF_H

#endif
