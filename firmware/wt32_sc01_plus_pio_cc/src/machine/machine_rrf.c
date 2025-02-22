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

    char *saveptr;
    char *line = strtok_r(gcode_copy, "\n", &saveptr);
    while (line != NULL) {
        _df(0, "Sending: %s", line);
        // Use the wrapper function here:
        serial_write(rrf_self->uart, (const uint8_t *)line, strlen(line));
        serial_write(rrf_self->uart, (const uint8_t *)"\n", 1);
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(gcode_copy);
}

static bool _machine_rrf_has_response(machine_rrf_t *self) {
    return serial_available(self->uart);
}

static bool _machine_rrf_read_response(machine_rrf_t *self, char *buffer, size_t buffer_size) {
    //_d(0, "Reading serial...\n");
    size_t len = serial_read_line_buf(self->uart, buffer, buffer_size, READ_TIMEOUT_MS);
    if (len > 0) {
        buffer[len] = '\0'; // Null-terminate the string

        _df(0, "Received: (%lu) %s (%d)\n", buffer_size, buffer, strlen(buffer));
        return true;
    }

    return false;
}

static void _machine_rrf_proc_machine_state(machine_rrf_t *self, const char *cmd)
{
    _machine_rrf_send_gcode((machine_interface_t*)self, cmd);
    char response_buffer[4096]; // Adjust size as needed
    if (_machine_rrf_read_response(self, response_buffer, sizeof(response_buffer))) {
        _machine_rrf_parse_json_response(self, response_buffer);
        if (!self->connected) {
            self->connected = true;
            machine_interface_connected_updated(&self->base);

            machine_interface_position_updated(&self->base);
            machine_interface_wcs_updated(&self->base);
            machine_interface_home_updated(&self->base);
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

        if (rrf_self->input_sel) {
          char cmd2[64];
          snprintf(cmd2, sizeof(cmd2), "M409 K\"%s\"", rrf_self->input_sel);
          _machine_rrf_proc_machine_state(rrf_self, cmd2);
        }
    }
    if (poll_state & MACHINE_POSITION_EXT) {
        _machine_rrf_proc_machine_state(rrf_self, "M409 K\"move.axes[]\" F\"d5,v\"");
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
    } else if (strcmp(key, "inputs[].name") == 0) {
        cJSON *name_json = NULL;
        int i = 0;
        cJSON_ArrayForEach(name_json, result_json) {
            if (cJSON_IsString(name_json)) {
              const char *name = name_json->valuestring;

              if (strcmp(name, "Aux") == 0) {
                self->input_idx = i;
                char input_sel_str[64];
                snprintf(input_sel_str, sizeof(input_sel_str), "inputs[%d].axesRelative", self->input_idx);
                self->input_sel = strdup(input_sel_str); // Allocate and copy
              }
            }
            ++i;
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

machine_rrf_t* machine_rrf_create(int rrf_serial_num, uint16_t sleep_ms, int tx_pin, int rx_pin) {
    machine_rrf_t *self = (machine_rrf_t *)malloc(sizeof(machine_rrf_t));
    if (!self) {
        _d(2,  "Failed to allocate memory for machine_rrf");
        return NULL;
    }

    return machine_rrf_init(self, rrf_serial_num, sleep_ms, tx_pin, rx_pin);
}

void _machine_rrf_find_input(machine_rrf_t *self) {
    char cmd1[64];
    snprintf(cmd1, sizeof(cmd1), "M409 K\"inputs[].name\"");
    _machine_rrf_proc_machine_state(self, cmd1);
}

machine_rrf_t* machine_rrf_init(machine_rrf_t *self, int rrf_serial_num, uint16_t sleep_ms, int tx_pin, int rx_pin) {
    // Initialize the base class part
    machine_interface_init(&self->base, sleep_ms);

    // Initialize RRF-specific members
    self->connected = false;
    self->input_sel = NULL; // Initialize to NULL
    self->input_idx = 0;

    // Initialize UART using the wrapper
    self->uart = serial_init(rrf_serial_num, 115200, CFG_SERIAL_8N1, rx_pin, tx_pin);
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

    _d(0, "Initialized RRF machine...\n");

    _machine_rrf_find_input(self);

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
