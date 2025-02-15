#include "machine_rrf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "machine/arduino_serial_wrapper.h"
#include "debug.h"

static const char *TAG = "machine_rrf";

// --- Helper Functions ---

// RRF-specific helper functions (private)
static void _machine_rrf_send_gcode(machine_interface_t *self, const char *gcode);
static bool _machine_rrf_has_response(machine_rrf_t *self);
static bool _machine_rrf_read_response(machine_rrf_t *self, char *buffer, size_t buffer_size);
static void _machine_rrf_update_machine_state(machine_interface_t *self, uint32_t poll_state);
static bool _machine_rrf_is_connected(machine_interface_t *self);
static void _machine_rrf_list_files(machine_interface_t *self, const char *path);
static void _machine_rrf_run_macro(machine_interface_t *self, const char *macro_name);
static void _machine_rrf_start_job(machine_interface_t *self, const char *job_name);
static void _machine_rrf_continuous_stop(machine_interface_t *self);
static void _machine_rrf_continuous_move(machine_interface_t *self, const char axis, float feed, int direction);

// JSON parsing helpers (private)
static void _machine_rrf_parse_json_response(machine_rrf_t *self, const char *json_response);
static void _machine_rrf_parse_m409_response(machine_rrf_t *self, cJSON *json_obj);
static void _machine_rrf_parse_move_axes_brief(machine_rrf_t *self, cJSON *axes_array);
static void _machine_rrf_parse_move_axes_ext(machine_rrf_t *self, cJSON *axes_array);
static void _machine_rrf_parse_move_axes(machine_rrf_t *self, cJSON *axes_array);
static void _machine_rrf_parse_globals(machine_rrf_t *self, cJSON *globals_obj);
static void _machine_rrf_parse_m20_response(machine_rrf_t *self, cJSON *json_obj);


static int machine_rrf_axis_idx(char axis) {
    switch (axis) {
        case 'X': return 0;
        case 'Y': return 1;
        case 'Z': return 2;
        // Add cases for U, V, A, B if needed
        default:  return -1; // Indicate invalid axis
    }
}

// --- RRF Method Implementations ---

static void _machine_rrf_send_gcode(machine_interface_t *self, const char *gcode) {
    machine_rrf_t *rrf_self = (machine_rrf_t *)self; // Cast to the derived type

     char *gcode_copy = strdup(gcode);
    if (!gcode_copy) {
        _d(2,  "Failed to allocate memory for G-code copy");
        return;
    }

    char *line = strtok(gcode_copy, "\n");
    while (line != NULL) {
        _df(0, "Sending: %s", line);
        // Use the wrapper function here:
        serial_write(rrf_self->uart, (const uint8_t *)line, strlen(line));
        serial_write(rrf_self->uart, (const uint8_t *)"\n", 1);
        line = strtok(NULL, "\n");
    }
    free(gcode_copy);
}

static bool _machine_rrf_has_response(machine_rrf_t *self) {
    return serial_available(self->uart);
}

static bool _machine_rrf_read_response(machine_rrf_t *self, char *buffer, size_t buffer_size) {
    size_t len = serial_read_line_buf(self->uart, buffer, buffer_size, READ_TIMEOUT_MS);
    if (len > 0) {
        buffer[len] = '\0'; // Null-terminate the string

        _df(0, "Received: %s", buffer);
        return true;
    }

    return false;
}

static void _machine_rrf_proc_machine_state(machine_rrf_t *self, const char *cmd)
{
    _machine_rrf_send_gcode((machine_interface_t*)self, cmd);
    char response_buffer[1024]; // Adjust size as needed
    if (_machine_rrf_read_response(self, response_buffer, sizeof(response_buffer))) {
        _machine_rrf_parse_json_response(self, response_buffer);
        if (!self->connected) {
            machine_interface_position_updated(&self->base);
            machine_interface_wcs_updated(&self->base);
            machine_interface_home_updated(&self->base);
        }
        if (!self->connected) {
            self->connected = true;
            machine_interface_connected_updated(&self->base);
        }
    } else {
        _df(1, "Timeout or Error: %s", cmd);
        if (self->connected) {
            self->connected = false;
            machine_interface_connected_updated(&self->base);
        }
    }
}

static void _machine_rrf_update_machine_state(machine_interface_t *self, uint32_t poll_state) {
    machine_rrf_t *rrf_self = (machine_rrf_t *)self;

    if (poll_state & MACHINE_POSITION) {
        // _machine_rrf_update_feed_multiplier_async(rrf_self);
        // _machine_rrf_update_wcs_async(rrf_self);
        char cmd1[64];
        snprintf(cmd1, sizeof(cmd1), "M409 K\"move.axes[]\" F\"d5,f\"");
        _machine_rrf_proc_machine_state(rrf_self, cmd1);

        char cmd2[64];
        snprintf(cmd2, sizeof(cmd2), "M409 K\"%s\"", rrf_self->input_sel ? rrf_self->input_sel : "");
        _machine_rrf_proc_machine_state(rrf_self, cmd2);
    }
    if (poll_state & MACHINE_POSITION_EXT) {
        _machine_rrf_proc_machine_state(rrf_self, "M409 K\"move.axes[]\" F\"d5\"");
    }
    if (poll_state & NETWORK) {
        // _machine_rrf_update_network_info_async(rrf_self);
    }
    if (poll_state & JOB_STATUS) {
        //_machine_rrf_update_current_job_async(rrf_self);
    }
    if (poll_state & MESSAGES_AND_DIALOGS) {
        //_machine_rrf_update_message_box_async(rrf_self);
    }
    if (poll_state & END_STOPS) {
        //_machine_rrf_update_endstops_async(rrf_self);
    }
    if (poll_state & PROBES) {
        //_machine_rrf_update_probe_vals_async(rrf_self);
    }
    if (poll_state & SPINDLE) {
        //_machine_rrf_update_spindles_async(rrf_self);
    }
    if (poll_state & TOOLS) {
        //_machine_rrf_update_tools_async(rrf_self);
    }
}

static bool _machine_rrf_is_connected(machine_interface_t *self) {
    return ((machine_rrf_t *)self)->connected;
}

static void _machine_rrf_list_files(machine_interface_t *self, const char *path) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "M20 S2 P\"/%s/\"", path);
    machine_interface_send_gcode(self, cmd, 0); // Use base class send_gcode
}

static void _machine_rrf_run_macro(machine_interface_t *self, const char *macro_name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "M98 P\"%s\"", macro_name);
    _machine_rrf_send_gcode(self, cmd); // Use RRF-specific _send_gcode
}

static void _machine_rrf_start_job(machine_interface_t *self, const char *job_name) {
    char cmd1[128];
    char cmd2[64];
    snprintf(cmd1, sizeof(cmd1), "M23 %s", job_name);
    snprintf(cmd2, sizeof(cmd2), "M24");
    _machine_rrf_send_gcode(self, cmd1);
    _machine_rrf_send_gcode(self, cmd2);
}

static void _machine_rrf_continuous_stop(machine_interface_t *self) {
    _machine_rrf_send_gcode(self, "M98 P\"pendant-continuous-stop.g\"");
}

static void _machine_rrf_continuous_move(machine_interface_t *self, const char axis, float feed, int direction) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "M98 P\"pendant-continuous-run.g\" A\"%c\" F%u D%u", axis, (unsigned int)feed, direction);
    _machine_rrf_send_gcode(self, cmd);
}

// --- JSON Parsing ---
static void _machine_rrf_parse_move_axes_brief(machine_rrf_t *self, cJSON *axes_array) {
    bool updated = false;
    if (!cJSON_IsArray(axes_array)) {
        return;
    }

    cJSON *axis = NULL;
    int i = 0;
    cJSON_ArrayForEach(axis, axes_array) {
        cJSON *machine_pos_json = cJSON_GetObjectItemCaseSensitive(axis, "machinePosition");
        cJSON *wcs_pos_json = cJSON_GetObjectItemCaseSensitive(axis, "userPosition");

        if (cJSON_IsNumber(machine_pos_json) && cJSON_IsNumber(wcs_pos_json)) {
            float machine_pos = machine_pos_json->valuedouble;
            float wcs_pos = wcs_pos_json->valuedouble;

            if (self->base.position[i] != machine_pos || self->base.wcs_position[i] != wcs_pos) {
                updated = true;
            }
            self->base.position[i] = machine_pos;
            self->base.wcs_position[i] = wcs_pos;
        }
        i++;
    }
    if (updated) {
        machine_interface_position_updated(&self->base);
    }
}

static void _machine_rrf_parse_move_axes_ext(machine_rrf_t *self, cJSON *axes_array) {
    bool updated = false;
    bool home_updated = false;

    if (!cJSON_IsArray(axes_array)) {
        return;
    }

    cJSON *axis = NULL;
    cJSON_ArrayForEach(axis, axes_array) {
        cJSON *name_json = cJSON_GetObjectItemCaseSensitive(axis, "letter");
        cJSON *homed_json = cJSON_GetObjectItemCaseSensitive(axis, "homed");
        cJSON *machine_pos_json = cJSON_GetObjectItemCaseSensitive(axis, "machinePosition");
        cJSON *wcs_pos_json = cJSON_GetObjectItemCaseSensitive(axis, "userPosition");

        if (cJSON_IsString(name_json) && cJSON_IsBool(homed_json) &&
            cJSON_IsNumber(machine_pos_json) && cJSON_IsNumber(wcs_pos_json)) {

            const char *name = name_json->valuestring;
            int i = machine_rrf_axis_idx(name[0]); // Assuming single-character axis names
            if (i < 0) continue; // Skip unknown axes

            bool homed = cJSON_IsTrue(homed_json);
            float machine_pos = machine_pos_json->valuedouble;
            float wcs_pos = wcs_pos_json->valuedouble;

            if (self->base.axes_homed[i] != homed) {
                home_updated = true;
            }
            self->base.axes_homed[i] = homed;

            if (self->base.position[i] != machine_pos || self->base.wcs_position[i] != wcs_pos) {
                updated = true;
            }
            self->base.position[i] = machine_pos;
            self->base.wcs_position[i] = wcs_pos;
        }
    }
    if (updated) {
        machine_interface_position_updated(&self->base);
    }
    if (home_updated) {
        machine_interface_home_updated(&self->base);
    }
}

static void _machine_rrf_parse_move_axes(machine_rrf_t *self, cJSON *axes_array) {
    if (!cJSON_IsArray(axes_array)) return;

    cJSON *first_axis = cJSON_GetArrayItem(axes_array, 0);
    if (first_axis && cJSON_GetObjectItemCaseSensitive(first_axis, "letter")) {
        _machine_rrf_parse_move_axes_ext(self, axes_array);
    } else {
        _machine_rrf_parse_move_axes_brief(self, axes_array);
    }
}

static void _machine_rrf_parse_globals(machine_rrf_t *self, cJSON *globals_obj) {
    // Handle global variables if needed
}

static void _machine_rrf_parse_m20_response(machine_rrf_t *self, cJSON *json_obj)
{
    cJSON *jdir = cJSON_GetObjectItemCaseSensitive(json_obj, "dir");
    cJSON *files = cJSON_GetObjectItemCaseSensitive(json_obj, "files");

    if (!cJSON_IsString(jdir) || !cJSON_IsArray(files)) {
        _d(2,  "Invalid M20 response format");
        return;
    }

    const char *dir_str = jdir->valuestring;
    // Remove leading and trailing slashes and quotes from dir_str
    while (*dir_str == '/' || *dir_str == '"') {
        dir_str++;
    }
    size_t dir_len = strlen(dir_str);
    while (dir_len > 0 && (dir_str[dir_len - 1] == '/' || dir_str[dir_len - 1] == '"')) {
        dir_len--;
    }
    char clean_dir[64]; // Adjust size as needed
    if (dir_len >= sizeof(clean_dir)) {
        _df(2, "Directory name too long: %.*s", (int)dir_len, dir_str);
        return;
    }
    strncpy(clean_dir, dir_str, dir_len);
    clean_dir[dir_len] = '\0';

    // Count the number of files
    int num_files = cJSON_GetArraySize(files);

    // Allocate memory for the filenames (array of char*)
    const char **filenames = (const char **)malloc(num_files * sizeof(char *));
    if (!filenames) {
        _d(2,  "Failed to allocate memory for filenames");
        return;
    }

    // Extract filenames and store them
    cJSON *file = NULL;
    int i = 0;
    cJSON_ArrayForEach(file, files) {
        if (cJSON_IsString(file)) {
            // Allocate memory for the filename and copy it
            filenames[i] = strdup(file->valuestring);
            if (!filenames[i]) {
                _d(2,  "Failed to allocate memory for filename");
                // Free previously allocated filenames
                for (int j = 0; j < i; j++) {
                    free((void *)filenames[j]);
                }
                free(filenames);
                return;
            }
            i++;
        }
    }

    // Find the correct file list entry or create a new one
    int filelist_index = -1;
    for (int j = 0; j < MAX_FILE_LISTS; j++) {
        if (self->base.filelists[j].fdir && strcmp(self->base.filelists[j].fdir, clean_dir) == 0) {
            filelist_index = j;
            break;
        }
    }
    if (filelist_index == -1) { // Not found, find an empty slot
        for (int j = 0; j < MAX_FILE_LISTS; j++) {
            if (self->base.filelists[j].fdir == NULL) {
                filelist_index = j;
                self->base.filelists[j].fdir = strdup(clean_dir); // Store the directory
                break;
            }
        }
    }

    if (filelist_index != -1) {
        // Free any previously stored filenames
        if (self->base.filelists[filelist_index].files) {
            for (int j = 0; self->base.filelists[filelist_index].files[j] != NULL; j++) {
                free((void *)self->base.filelists[filelist_index].files[j]);
            }
            free((void *)self->base.filelists[filelist_index].files);
        }

        self->base.filelists[filelist_index].files = filenames;
        machine_interface_files_updated(&self->base, clean_dir);
    } else { // No empty slots
        _d(2,  "No empty file list slots available");
        // Free the newly allocated filenames
        for (int j = 0; j < num_files; j++) {
            if (filenames[j]) free((void *)filenames[j]);
        }
        free(filenames);
    }
}

static void _machine_rrf_parse_m409_response(machine_rrf_t *self, cJSON *json_obj) {
    cJSON *key_json = cJSON_GetObjectItemCaseSensitive(json_obj, "key");
    cJSON *result_json = cJSON_GetObjectItemCaseSensitive(json_obj, "result");

    if (!cJSON_IsString(key_json) || !result_json) {
        _d(2,  "Invalid M409 response format");
        return;
    }

    const char *key = key_json->valuestring;

    if (strcmp(key, "move.axes") == 0 || strcmp(key, "move.axes[]") == 0) {
        _machine_rrf_parse_move_axes(self, result_json);
    } else if (strcmp(key, "global") == 0) {
        _machine_rrf_parse_globals(self, result_json);
    } else if (strcmp(key, "job") == 0) {
        // self.job = { ... } // Parse job information
    } else if (strcmp(key, "move.workplaceNumber") == 0) {
        if (cJSON_IsNumber(result_json) && result_json->valueint != self->base.wcs) {
            self->base.wcs = result_json->valueint;
            machine_interface_wcs_updated(&self->base);
        }
    } else if (strcmp(key, "move.currentMove") == 0) {
        // self.feed = ...; self.feed_req = ...; // Parse feed information
    } else if (strcmp(key, "move.speedFactor") == 0) {
        if (cJSON_IsNumber(result_json)) {
            float speed_factor = result_json->valuedouble;
            if (self->base.feed_multiplier != speed_factor) {
                self->base.feed_multiplier = speed_factor;
                machine_interface_feed_updated(&self->base);
            }
        }
    } else if (strcmp(key, "network") == 0) {
        // self.network = [ ... ]; // Parse network information
    } else if (strcmp(key, "state.messageBox") == 0) {
        // self.message_box = result_json; // Parse message box
        // machine_interface_dialogs_updated(&self->base);
    } else if (strcmp(key, "sensors.probes[].value[]") == 0) {
        // self.probes = result_json; // Parse probe values
        // machine_interface_sensors_updated(&self->base);
    } else if (strcmp(key, "state.thisInput") == 0) {
        if (cJSON_IsNumber(result_json)) {
            self->input_idx = result_json->valueint;
            char input_sel_str[64];
            snprintf(input_sel_str, sizeof(input_sel_str), "inputs[%d].axesRelative", self->input_idx);
            self->input_sel = strdup(input_sel_str); // Allocate and copy
        }
    } else if (self->input_sel && strcmp(key, self->input_sel) == 0) {
        if (cJSON_IsBool(result_json)) {
            bool move_relative = cJSON_IsTrue(result_json);
            if (self->base.move_relative != move_relative) {
                self->base.move_relative = move_relative;
                machine_interface_position_updated(&self->base);
            }
        }
    } else {
        _df(1, "Unhandled M409 key: %s", key);
    }
}

static void _machine_rrf_parse_json_response(machine_rrf_t *self, const char *json_response) {
    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        _df(2, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            _df(2, "Error before: %s", error_ptr);
        }
        return;
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "dir")) {
        _machine_rrf_parse_m20_response(self, root);
    } else if (cJSON_GetObjectItemCaseSensitive(root, "key") || !cJSON_GetObjectItemCaseSensitive(root, "result")) {
        _machine_rrf_parse_m409_response(self, root);
    } else {
        _df(2, "Unrecognized JSON response: %s", json_response);
    }

    cJSON_Delete(root);
}

// --- Constructor ---

machine_rrf_t* machine_rrf_create(uint16_t sleep_ms, int tx_pin, int rx_pin) {
    machine_rrf_t *self = (machine_rrf_t *)malloc(sizeof(machine_rrf_t));
    if (!self) {
        _d(2,  "Failed to allocate memory for machine_rrf");
        return NULL;
    }

    return machine_rrf_init(self, sleep_ms, tx_pin, rx_pin);
}

machine_rrf_t* machine_rrf_init(machine_rrf_t *self, uint16_t sleep_ms, int tx_pin, int rx_pin) {
    // Initialize the base class part
    machine_interface_init(&self->base, sleep_ms);

    // Initialize RRF-specific members
    self->connected = false;
    self->input_sel = NULL; // Initialize to NULL
    self->input_idx = 0;
    
    // Initialize UART using the wrapper
    self->uart = serial_init(RRF_SERIAL_UART_NUM, 115200, CFG_SERIAL_8N1, rx_pin, tx_pin);
    if (!self->uart) {
        _d(2,  "Failed to initialize serial port");
        return NULL;
    }

    // Override base class methods (important for correct behavior)
    self->base.send_gcode = machine_interface_send_gcode; // Use the generic wrapper
    self->base._send_gcode = _machine_rrf_send_gcode;
    self->base._update_machine_state = _machine_rrf_update_machine_state;
    self->base.is_connected = _machine_rrf_is_connected;
    self->base.list_files = _machine_rrf_list_files;
    self->base.run_macro = _machine_rrf_run_macro;
    self->base.start_job = _machine_rrf_start_job;
    self->base.move_continuous = _machine_rrf_continuous_move;
    self->base.move_continuous_stop = _machine_rrf_continuous_stop;
    self->base._continuous_move = _machine_rrf_continuous_move;
    self->base._continuous_stop = _machine_rrf_continuous_stop;

    return self;
}

// --- Destructor ---

void machine_rrf_deinit(machine_rrf_t *self) {
    if (self) {
        // Clean up RRF-specific resources
        serial_end(self->uart);
        if (self->input_sel) {
            free((void *)self->input_sel); // Free the duplicated string
        }

        // Clean up base class resources
        machine_interface_deinit(&self->base);
    }
}

void machine_rrf_destroy(machine_rrf_t *self) {
    if (self) {
        machine_rrf_deinit(self);

        // Free the structure itself
        free(self);
    }
}

void machine_rrf_task_loop_iter(machine_rrf_t *self) {
    // Delegate to the base class implementation, which will call our overridden methods
    machine_interface_task_loop_iter(&self->base);
}









#if 0
// machine_rrf.c
#include "machine_rrf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ArduinoJson.h" //  ArduinoJson library

static const char *TAG = "machine_rrf";

// --- Initialization ---
void machine_rrf_init(machine_rrf_t *machine, uint16_t sleep_ms) {
    if (!machine) return;

    machine_interface_init(&machine->base, sleep_ms); // Initialize base class
    machine->uart = &Serial2; //  HardwareSerial (ESP32-S3 has Serial2)
    machine->uart->begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    machine->connected = false;
    machine->input_sel = ""; // Initialize
    machine->input_idx = -1;
    machine_interface_connected_updated(&machine->base); //  initial state
}

// --- Overridden Machine Interface Functions ---

void _machine_rrf_send_gcode(machine_interface_t *machine, const char *gcode) {
    machine_rrf_t *rrf_machine = (machine_rrf_t *)machine; // Cast to derived class
    if (!rrf_machine || !rrf_machine->uart || !gcode) return;

    rrf_machine->uart->print(gcode);
    rrf_machine->uart->print('\n');
    //_df(0, "Sent G-code: %s", gcode); //  debug logging
}

bool _machine_rrf_has_response(machine_interface_t *machine) {
    machine_rrf_t *rrf_machine = (machine_rrf_t *)machine;
    if (!rrf_machine || !rrf_machine->uart) return false;
    return rrf_machine->uart->available() > 0;
}

const char *_machine_rrf_read_response(machine_interface_t *machine) {
    machine_rrf_t *rrf_machine = (machine_rrf_t *)machine;
    if (!rrf_machine || !rrf_machine->uart) return "";

    static char response_buffer[512]; //  static buffer for response
    memset(response_buffer, 0, sizeof(response_buffer)); // Clear buffer

    size_t len = 0;
    while (rrf_machine->uart->available() > 0 && len < sizeof(response_buffer) - 1) {
        char c = rrf_machine->uart->read();
        response_buffer[len++] = c;
        if (c == '\n') break; //  end of line
    }
    response_buffer[len] = '\0'; // Null-terminate
    //_df(0, "Received: %s", response_buffer); //  debug logging
    return response_buffer;
}

void _machine_rrf_update_machine_state(machine_interface_t *machine, poll_state_t poll_state)
{
    if(!machine) return;
    //  async processing, using FreeRTOS tasks instead of uasyncio
    if (poll_state & MACHINE_POSITION) {
        _machine_rrf_send_gcode(machine, "M409 K\"move.axes[]\" F\"d5,f\"");
        _machine_rrf_send_gcode(machine, "M409 K\"move.speedFactor\"");
        _machine_rrf_send_gcode(machine, "M409 K\"move.workplaceNumber\"");
        if(strlen(((machine_rrf_t*)machine)->input_sel) > 0)
        {
            char input_sel_cmd[128];
            snprintf(input_sel_cmd, sizeof(input_sel_cmd), "M409 K\"%s\"", ((machine_rrf_t*)machine)->input_sel);
            _machine_rrf_send_gcode(machine, input_sel_cmd);
        }
    }
    if (poll_state & MACHINE_POSITION_EXT)    _machine_rrf_send_gcode(machine, "M409 K\"move.axes[]\" F\"d5\"");
    if (poll_state & NETWORK)                 _machine_rrf_send_gcode(machine, "M409 K\"network\"");
    if (poll_state & JOB_STATUS)              _machine_rrf_send_gcode(machine, "M409 K\"job\" F\"d3\"");
    if (poll_state & MESSAGES_AND_DIALOGS)    _machine_rrf_send_gcode(machine, "M409 K\"state.messageBox\"");
    if (poll_state & END_STOPS)               _machine_rrf_send_gcode(machine, "M409 K\"sensors.endstops[]\"");
    if (poll_state & PROBES)                  _machine_rrf_send_gcode(machine, "M409 K\"sensors.probes[].value[]\"");
    if (poll_state & SPINDLE)                 {
        machine_interface_spindles_tools_updated(machine);
        _d(0,  "Spindle update: TODO");
    } //  placeholders
    if (poll_state & TOOLS)                   {
        machine_interface_spindles_tools_updated(machine);
        _d(0,  "Tools update: TODO");
    } //  placeholders

    //  process responses
    while(_machine_rrf_has_response(machine))
    {
        const char *response = _machine_rrf_read_response(machine);
        if(strlen(response) > 0)
        {
            _machine_rrf_parse_json_response(machine, response);
            if(!((machine_rrf_t*)machine)->connected)
            {
                machine_interface_position_updated(machine);
                machine_interface_wcs_updated(machine);
                machine_interface_home_updated(machine);
                //  find input -  placeholder for now
            }
            if(((machine_rrf_t*)machine)->connected == false)
            {
                ((machine_rrf_t*)machine)->connected = true;
                machine_interface_connected_updated(machine);
            }
        }
    }
}

void machine_rrf_list_files(machine_interface_t *machine, const char *path)
{
    if(!machine || !path) return;
    char gcode[128];
    snprintf(gcode, sizeof(gcode), "M20 S2 P\"/%s/\"", path);
    machine_interface_send_gcode(machine, gcode, 0); //  poll_state not used here
}

void machine_rrf_run_macro(machine_interface_t *machine, const char *macro_name)
{
    if(!machine || !macro_name) return;
    char gcode[128];
    snprintf(gcode, sizeof(gcode), "M98 P\"%s\"", macro_name);
    _machine_rrf_send_gcode(machine, gcode); // Use internal send
}

void machine_rrf_start_job(machine_interface_t *machine, const char *job_name)
{
    if(!machine || !job_name) return;
    char gcode[128];
    snprintf(gcode, sizeof(gcode), "M23 %s", job_name);
    _machine_rrf_send_gcode(machine, gcode); // Use internal send
    _machine_rrf_send_gcode(machine, "M24"); // Use internal send
}

bool machine_rrf_is_connected(machine_interface_t *machine)
{
    if(!machine) return false;
    return ((machine_rrf_t*)machine)->connected;
}

void _machine_rrf_continuous_stop(machine_interface_t *machine)
{
    if(!machine) return;
    _machine_rrf_send_gcode(machine, "M98 P\"pendant-continuous-stop.g\"");
}

void _machine_rrf_continuous_move(machine_interface_t *machine, char axis, float feed, float direction)
{
    if(!machine) return;
    char gcode[128];
    snprintf(gcode, sizeof(gcode), "M98 P\"pendant-continuous-run.g\" A\"%c\" F%.3f D%.3f", axis, feed, direction);
    _machine_rrf_send_gcode(machine, gcode);
}

// --- JSON Parsing Helpers ---
void _machine_rrf_parse_json_response(machine_interface_t *machine, const char *json_resp) {
    if (!machine || !json_resp) return;

    //  use ArduinoJson for parsing
    StaticJsonDocument<2048> doc; //  adjust size as needed
    DeserializationError error = deserializeJson(doc, json_resp);

    if (error) {
        _df(2, "JSON parsing failed: %s, Response: %s", error.c_str(), json_resp);
        return;
    }

    if (doc.containsKey("dir")) {
        _machine_rrf_parse_m20_response(machine, json_resp);
    } else if (doc.containsKey("key")) {
        _machine_rrf_parse_m409_response(machine, json_resp);
    } else {
        _df(1, "Unrecognized JSON response: %s", json_resp);
    }
}

void _machine_rrf_parse_m20_response(machine_interface_t *machine, const char *json_resp)
{
    if(!machine || !json_resp) return;

    StaticJsonDocument<1024> doc; //  adjust size
    DeserializationError error = deserializeJson(doc, json_resp);
    if(error) { _d(2,  "m20 parsing error"); return; }

    const char* jdir = doc["dir"];
    if(!jdir) return;

    //  remove leading slash if present
    if (jdir[0] == '/') {
        jdir++; //  move pointer past the slash
    }

    JsonArray files = doc["files"];
    if(files.isNull()) return;

    //  simplified file handling -  void* for now
    //  allocate memory to store filenames
    char** file_list = (char**)malloc(files.size() * sizeof(char*));
    if(!file_list) { _d(2,  "malloc failed"); return; }

    for(size_t i = 0; i < files.size(); i++)
    {
        const char* filename = files[i];
        if(filename)
        {
            file_list[i] = (char*)malloc(strlen(filename) + 1); // +1 for null terminator
            if(file_list[i])
            {
                strcpy(file_list[i], filename);
            } else {
                _d(2,  "malloc failed for filename");
                //  clean up previously allocated memory
                for(size_t j = 0; j < i; j++) {
                    free(file_list[j]);
                }
                free(file_list);
                return;
            }
        }
    }

    //  store file list in machine->files (as void*)
    machine->files = (void*)file_list;
    machine_interface_files_updated(machine, jdir);
}

void _machine_rrf_parse_m409_response(machine_interface_t *machine, const char *json_resp) {
    if (!machine || !json_resp) return;

    StaticJsonDocument<1536> doc; //  adjust size
    DeserializationError error = deserializeJson(doc, json_resp);
    if(error) { _d(2,  "m409 parsing error"); return; }

    const char *key = doc["key"];
    if (!key) return;

    if (strcmp(key, "move.axes") == 0 || strcmp(key, "move.axes[]") == 0) {
        _machine_rrf_parse_move_axes(machine, json_resp);
    } else if (strcmp(key, "global") == 0) {
        _machine_rrf_parse_globals(machine, json_resp);
    } else if(strcmp(key, "job") == 0) {
        //  job parsing -  simplified for now
        JsonObject result = doc["result"];
        if(!result.isNull())
        {
            //  example of accessing a nested value
            if(result.containsKey("file") && result["file"].containsKey("fileName"))
            {
                const char* filename = result["file"]["fileName"];
                //  store filename (allocate memory)
                if(filename)
                {
                    char* job_filename = (char*)malloc(strlen(filename) + 1);
                    if(job_filename)
                    {
                        strcpy(job_filename, filename);
                        machine->job = (void*)job_filename; //  store as void*
                    } else {
                        _d(2,  "malloc failed for job filename");
                    }
                }
            }
        }
    } else if(strcmp(key, "move.workplaceNumber") == 0) {
        int wcs = doc["result"];
        if(wcs != machine->wcs)
        {
            machine->wcs = wcs;
            machine_interface_wcs_updated(machine);
        }
    } else if(strcmp(key, "move.current_move") == 0) {
        //  feed parsing
        machine->feed = doc["result"]["topSpeed"];
        machine->feed_req = doc["result"]["requestedSpeed"];
    } else if(strcmp(key, "move.speedFactor") == 0) {
        float feed_multiplier = doc["result"];
        if(machine->feed_multiplier != feed_multiplier)
        {
            machine->feed_multiplier = feed_multiplier;
            machine_interface_feed_updated(machine);
        }
    } else if (strcmp(key, "network") == 0) {
        //  network parsing -  simplified for now
        //  allocate memory for network info (example)
        char* network_info = (char*)malloc(256); //  adjust size
        if(network_info)
        {
            //  example: store hostname
            snprintf(network_info, 256, "Hostname: %s", doc["result"]["hostname"].as<const char*>());
            machine->network = (void*)network_info;
        } else {
            _d(2,  "malloc failed for network info");
        }
    } else if (strcmp(key, "state.messageBox") == 0) {
        //  message box parsing
        if (!doc["result"].isNull()) {
            //  store message box (allocate memory)
            const char* message = doc["result"]["msg"];
            if(message)
            {
                char* msg_box = (char*)malloc(strlen(message) + 1);
                if(msg_box)
                {
                    strcpy(msg_box, message);
                    machine->message_box = (void*)msg_box; //  store as void*
                } else {
                    _d(2,  "malloc failed for message box");
                }
            }
        } else {
            machine->message_box = NULL; //  clear message box
        }
        machine_interface_dialogs_updated(machine);
    } else if (strcmp(key, "sensors.probes[].value[]") == 0) {
        //  probe value parsing
        JsonArray probes = doc["result"];
        if(!probes.isNull() && probes.size() >= 2)
        {
            machine->probes[0] = probes[0];
            machine->probes[1] = probes[1];
            machine_interface_sensors_updated(machine);
        }
    } else if (strcmp(key, "state.thisInput") == 0) {
        //  input parsing
        ((machine_rrf_t*)machine)->input_idx = doc["result"];
        char input_sel[64];
        snprintf(input_sel, sizeof(input_sel), "inputs[%d].axesRelative", ((machine_rrf_t*)machine)->input_idx);
        ((machine_rrf_t*)machine)->input_sel = strdup(input_sel); //  strdup allocates memory
    } else if (strcmp(key, ((machine_rrf_t*)machine)->input_sel) == 0) {
        //  move relative parsing
        bool move_relative = doc["result"];
        if(machine->move_relative != move_relative)
        {
            machine->move_relative = move_relative;
            machine_interface_position_updated(machine);
        }
    } else {
        _df(1, "Unknown M409 key: %s", key);
    }
}

void _machine_rrf_parse_move_axes(machine_interface_t *machine, const char *json_resp)
{
    if(!machine || !json_resp) return;

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json_resp);
    if(error) { _d(2,  "move_axes parsing error"); return; }

    JsonArray axes = doc["result"];
    if(axes.isNull() || axes.size() == 0) return;

    if(axes[0].containsKey("letter"))
    {
        _machine_rrf_parse_move_axes_ext(machine, json_resp);
    }
    else
    {
        _machine_rrf_parse_move_axes_brief(machine, json_resp);
    }
}

void _machine_rrf_parse_move_axes_brief(machine_interface_t *machine, const char *json_resp)
{
    if(!machine || !json_resp) return;

    StaticJsonDocument<512> doc; //  adjust size
    DeserializationError error = deserializeJson(doc, json_resp);
    if(error) { _d(2,  "move_axes_brief parsing error"); return; }

    JsonArray axes = doc["result"];
    if(axes.isNull()) return;

    bool updated = false;
    for(size_t i = 0; i < axes.size() && i < 3; i++)
    {
        float machine_pos = axes[i]["machinePosition"];
        float wcs_pos = axes[i]["userPosition"];

        if(machine->position[i] != machine_pos || machine->wcs_position[i] != wcs_pos)
        {
            updated = true;
        }
        machine->position[i] = machine_pos;
        machine->wcs_position[i] = wcs_pos;
    }
    if(updated) machine_interface_position_updated(machine);
}

void _machine_rrf_parse_move_axes_ext(machine_interface_t *machine, const char *json_resp)
{
    if(!machine || !json_resp) return;

    StaticJsonDocument<1024> doc; //  adjust size
    DeserializationError error = deserializeJson(doc, json_resp);
    if(error) { _d(2,  "move_axes_ext parsing error"); return; }

    JsonArray axes = doc["result"];
    if(axes.isNull()) return;

    bool updated = false;
    bool home_updated = false;

    for(size_t i = 0; i < axes.size() && i < 3; i++)
    {
        const char* name = axes[i]["letter"];
        if(!name) continue;
        int axi = _machine_interface_axis_idx(machine, name[0]);
        if(axi == -1) continue; //  invalid axis

        bool homed = axes[i]["homed"];
        float machine_pos = axes[i]["machinePosition"];
        float wcs_pos = axes[i]["userPosition"];

        if(machine->axes_homed[axi] != homed)
        {
            home_updated = true;
        }
        machine->axes_homed[axi] = homed;

        if(machine->position[axi] != machine_pos || machine->wcs_position[axi] != wcs_pos)
        {
            updated = true;
        }
        machine->position[axi] = machine_pos;
        machine->wcs_position[axi] = wcs_pos;
    }
    if(updated) machine_interface_position_updated(machine);
    if(home_updated) machine_interface_home_updated(machine);
}

void _machine_rrf_parse_globals(machine_interface_t *machine, const char *json_resp)
{
    //  globals parsing - not used in this example
    (void)machine; //  prevent unused variable warning
    (void)json_resp;
}

#endif