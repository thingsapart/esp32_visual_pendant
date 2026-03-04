#include "rrf_simulator_task.h"

#ifdef ENABLE_DEVICE_SIMULATOR_TASK

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "machine/rrf_machine_sim_stream.h"
#include "debug.h"

static const char *TAG = "RRF_DEV_SIM";

// --- Virtual Machine State ---
typedef struct {
    float m_pos[3];      // Machine Position
    float wcs_pos[3];    // Work Position (derived)
    float wcs_offset[3]; // G54 offset
    bool homed[3];
    int wcs_index;       // 1 = G54
    char status[20];     // "idle", "busy", etc.
    float feedrate;
    float spindle_rpm;
    int current_tool;
} virtual_machine_t;

static virtual_machine_t vm = {
    .m_pos = {10.0f, 10.0f, 50.0f},
    .wcs_pos = {0.0f, 0.0f, 0.0f},
    .wcs_offset = {0.0f, 0.0f, 0.0f},
    .homed = {true, true, true}, // Start homed for convenience
    .wcs_index = 1,
    .status = "idle",
    .feedrate = 1000.0f,
    .spindle_rpm = 0.0f,
    .current_tool = -1
};

// --- Helper: Response Generation ---

static void send_raw(RRFMachineSimStream* stream, const char* str) {
    stream->sim_write((const uint8_t*)str, strlen(str));
    stream->sim_write((const uint8_t*)"\n", 1);
    // LOGD(TAG, "TX: %s", str); 
}

static void send_json_response(RRFMachineSimStream* stream, const char* json_body) {
    send_raw(stream, json_body);
}

static void send_log_message(RRFMachineSimStream* stream, const char* msg) {
    char buf[256];
    // RRF sends log messages wrapped in a JSON packet if communicating via HTTP/DWC,
    // but over Serial it often just sends text. 
    // However, machine_rrf.c _serial_parse_json_response expects {"seq":..., "resp":"..."}
    // for async messages or just raw text lines if they don't look like JSON.
    // Let's send the JSON format the handler expects for log messages.
    snprintf(buf, sizeof(buf), "{\"seq\":0,\"resp\":\"%s\"}", msg);
    send_raw(stream, buf);
}

// --- Parameter Parsing ---
static float get_gcode_param(const char* line, char char_code, float def_val) {
    char key[4] = { ' ', char_code, '\0', '\0' }; // " X"
    char* p = strstr((char*)line, key);
    if (!p) {
        // Try at start of line
        if (line[0] == char_code) p = (char*)line;
    }
    
    if (p) {
        return atof(p + 1 + (p==line ? 0 : 1));
    }
    return def_val;
}

// --- MOS Probing Simulation ---

static void simulate_mos_probe(RRFMachineSimStream* stream, const char* line) {
    // Parse macro name: M98 P"G65xx.x.g"
    char* p_ptr = strstr((char*)line, "P\"");
    if (!p_ptr) return;
    char macro[32];
    sscanf(p_ptr + 2, "%31[^/\"/]", macro); // Read until quote

    LOGI(TAG, "Simulating MOS Macro: %s", macro);

    // Simulate "busy" state
    strcpy(vm.status, "busy");
    vTaskDelay(pdMS_TO_TICKS(500)); // Initial delay

    // Parse params common to MOS macros
    float j_start_x = get_gcode_param(line, 'J', vm.m_pos[0]);
    float k_start_y = get_gcode_param(line, 'K', vm.m_pos[1]);
    float l_start_z = get_gcode_param(line, 'L', vm.m_pos[2]);
    float h_size    = get_gcode_param(line, 'H', 20.0f);
    float i_size    = get_gcode_param(line, 'I', 20.0f);

    char buf[128];

    if (strstr(macro, "G6510.1")) { // Z Single-Surface Probe
        LOGI(TAG, "Simulating Z Probe...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Assume we found surface 5mm below start
        float z_found = vm.m_pos[2] - 5.0f;
        
        snprintf(buf, sizeof(buf), "global.mosWPSfcAxis[0]=Z");
        send_log_message(stream, buf);
        snprintf(buf, sizeof(buf), "global.mosWPSfcPos[0]=%.3f", z_found);
        send_log_message(stream, buf);
        
        send_log_message(stream, "MillenniumOS: Setting WCS Z origin to probed co-ordinate.");
    }
    else if (strstr(macro, "G6503.1") || strstr(macro, "G6502.1")) { // Rect Block/Pocket
        LOGI(TAG, "Simulating Rect Probe...");
        vTaskDelay(pdMS_TO_TICKS(1500)); 

        // Simulate a result slightly offset from approximate center
        float res_x = j_start_x + 0.5f;
        float res_y = k_start_y - 0.2f;
        // Use the operator-provided dimensions with small simulated error
        float res_w = h_size + 0.1f;
        float res_h = i_size + 0.05f;

        snprintf(buf, sizeof(buf), "global.mosWPCtrPos[0]={%.3f,%.3f}", res_x, res_y);
        send_log_message(stream, buf);
        snprintf(buf, sizeof(buf), "global.mosWPDims[0]={%.3f,%.3f}", res_w, res_h);
        send_log_message(stream, buf);
        send_log_message(stream, "global.mosWPDeg[0]=0.350");

        send_log_message(stream, "MillenniumOS: Setting WCS X,Y origin to the center of the rectangle block.");
    }
    else if (strstr(macro, "G6501.1") || strstr(macro, "G6500.1")) { // Circle Boss/Bore
        LOGI(TAG, "Simulating Circle Probe...");
        vTaskDelay(pdMS_TO_TICKS(1500));

        float res_x = j_start_x + 0.1f;
        float res_y = k_start_y + 0.1f;
        float res_r = h_size / 2.0f + 0.02f; // Half of operator-provided diameter + small error

        snprintf(buf, sizeof(buf), "global.mosWPCtrPos[0]={%.3f,%.3f}", res_x, res_y);
        send_log_message(stream, buf);
        snprintf(buf, sizeof(buf), "global.mosWPRad[0]=%.3f", res_r);
        send_log_message(stream, buf);

        send_log_message(stream, "MillenniumOS: Setting WCS X,Y origin to center of bore.");
    }
    else if (strstr(macro, "G6508.1")) { // Outside Corner
        LOGI(TAG, "Simulating Corner Probe...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Corner probe reports mosWPCnrPos (the corner intersection point)
        float cnr_x = j_start_x - 0.1f;
        float cnr_y = k_start_y + 0.15f;
        
        snprintf(buf, sizeof(buf), "global.mosWPCnrPos[0]={%.3f,%.3f}", cnr_x, cnr_y);
        send_log_message(stream, buf);
        
        // Quick mode (Q1) does not report rotation/center/dims
        send_log_message(stream, "MillenniumOS: Setting WCS X,Y origin to corner.");
    }

    strcpy(vm.status, "idle");
}

// --- Handler Implementations ---

static void handle_m409(RRFMachineSimStream* stream, const char* key) {
    char resp[2048]; // Needs to be large for axes array

    if (strstr(key, "move.axes")) {
        // Calculate user positions based on WCS offset
        float ux = vm.m_pos[0] - vm.wcs_offset[0];
        float uy = vm.m_pos[1] - vm.wcs_offset[1];
        float uz = vm.m_pos[2] - vm.wcs_offset[2];

        snprintf(resp, sizeof(resp), 
        "{\"key\":\"move.axes\",\"flags\":\"\",\"result\":["
        "{\"letter\":\"X\",\"machinePosition\":%.3f,\"homed\":%s,\"userPosition\":%.3f,\"workplaceOffsets\":[%.3f,0,0,0,0,0,0,0,0,0]},"
        "{\"letter\":\"Y\",\"machinePosition\":%.3f,\"homed\":%s,\"userPosition\":%.3f,\"workplaceOffsets\":[%.3f,0,0,0,0,0,0,0,0,0]},"
        "{\"letter\":\"Z\",\"machinePosition\":%.3f,\"homed\":%s,\"userPosition\":%.3f,\"workplaceOffsets\":[%.3f,0,0,0,0,0,0,0,0,0]}"
        "]}",
        vm.m_pos[0], vm.homed[0]?"true":"false", ux, vm.wcs_offset[0],
        vm.m_pos[1], vm.homed[1]?"true":"false", uy, vm.wcs_offset[1],
        vm.m_pos[2], vm.homed[2]?"true":"false", uz, vm.wcs_offset[2]
        );
        send_json_response(stream, resp);
    } 
    else if (strstr(key, "state.status")) {
        snprintf(resp, sizeof(resp), "{\"key\":\"state.status\",\"flags\":\"\",\"result\":\"%s\"}", vm.status);
        send_json_response(stream, resp);
    }
    else if (strstr(key, "move.currentMove")) {
        snprintf(resp, sizeof(resp), "{\"key\":\"move.currentMove\",\"flags\":\"\",\"result\":{\"requestedSpeed\":%.1f,\"topSpeed\":%.1f}}", vm.feedrate/60.0f, vm.feedrate/60.0f);
        send_json_response(stream, resp);
    }
    else if (strstr(key, "state.messageBox")) {
        // Simulating no message box. To simulate one, fill this JSON.
        snprintf(resp, sizeof(resp), "{\"key\":\"state.messageBox\",\"flags\":\"\",\"result\":null}");
        send_json_response(stream, resp);
    }
    else if (strstr(key, "spindles")) {
        snprintf(resp, sizeof(resp), "{\"key\":\"spindles[]\",\"flags\":\"\",\"result\":[{\"current\":%.1f,\"active\":%.1f}]}", vm.spindle_rpm, vm.spindle_rpm);
        send_json_response(stream, resp);
    }
    else if (strstr(key, "tools")) {
        // Return a tool list
        snprintf(resp, sizeof(resp), "{\"key\":\"tools[]\",\"flags\":\"\",\"result\":[{\"number\":0,\"name\":\"Probe\"},{\"number\":1,\"name\":\"EndMill 6mm\"}]}");
        send_json_response(stream, resp);
    }
    else if (strstr(key, "state.currentTool")) {
        snprintf(resp, sizeof(resp), "{\"key\":\"state.currentTool\",\"flags\":\"\",\"result\":%d}", vm.current_tool);
        send_json_response(stream, resp);
    }
    else if (strstr(key, "move.speedFactor")) {
        snprintf(resp, sizeof(resp), "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":100.0}");
        send_json_response(stream, resp);
    }
    else {
        // Generic empty response
        snprintf(resp, sizeof(resp), "{\"key\":\"%s\",\"flags\":\"\",\"result\":null}", key);
        send_json_response(stream, resp);
    }
}

static void handle_m20(RRFMachineSimStream* stream, const char* dir) {
    char resp[1024];
    if (strstr(dir, "macros")) {
        snprintf(resp, sizeof(resp), "{\"dir\":\"/macros\",\"files\":[\"probe_z.g\",\"home_all.g\",\"mesh_comp.g\",\"tool_change.g\",\"calibrate.g\"],\"err\":0}");
    } else {
        snprintf(resp, sizeof(resp), "{\"dir\":\"/gcodes\",\"files\":[\"bracket_op1.gcode\",\"bracket_op2.gcode\",\"face_stock.gcode\",\"drill_holes.gcode\"],\"err\":0}");
    }
    send_json_response(stream, resp);
}

// --- G-Code Logic ---

static void process_line(RRFMachineSimStream* stream, char* line) {
    LOGD(TAG, "RX: %s", line);

    if (strncmp(line, "M409", 4) == 0) {
        char* key_start = strstr(line, "K\"");
        if (key_start) {
            key_start += 2;
            char* key_end = strchr(key_start, '\"');
            if (key_end) {
                *key_end = '\0';
                handle_m409(stream, key_start);
            }
        }
    }
    else if (strncmp(line, "M20", 3) == 0) {
        char* p_start = strstr(line, "P\"");
        if (p_start) {
            p_start += 2;
            char* p_end = strchr(p_start, '\"');
            if (p_end) *p_end = '\0';
            handle_m20(stream, p_start);
        }
    }
    else if (strncmp(line, "M98", 3) == 0) {
        // Macro / Subroutine
        simulate_mos_probe(stream, line);
    }
    else if (strncmp(line, "G0", 2) == 0 || strncmp(line, "G1", 2) == 0) {
        strcpy(vm.status, "busy");
        
        // Handle G91 (Relative) if we tracked it, but for simplicity assuming absolute or simple parsing
        // We really should track G90/G91.
        
        float x = get_gcode_param(line, 'X', NAN);
        float y = get_gcode_param(line, 'Y', NAN);
        float z = get_gcode_param(line, 'Z', NAN);
        float f = get_gcode_param(line, 'F', NAN);

        if (!isnan(x)) vm.m_pos[0] = x; // Simplified absolute move
        if (!isnan(y)) vm.m_pos[1] = y;
        if (!isnan(z)) vm.m_pos[2] = z;
        if (!isnan(f)) vm.feedrate = f;

        // Instant completion for smooth UI testing
        strcpy(vm.status, "idle");
    }
    else if (strncmp(line, "G10", 3) == 0) {
        // G10 L20 Px X... (Set WCS)
        // Parse P (index), X, Y, Z
        float p = get_gcode_param(line, 'P', 0);
        float x = get_gcode_param(line, 'X', NAN);
        float y = get_gcode_param(line, 'Y', NAN);
        float z = get_gcode_param(line, 'Z', NAN);
        
        // G10 L20 sets the current position to the given value in the WCS
        // WCS Offset = MachinePos - NewUserPos
        if (p >= 1) {
            // Update WCS offset
            if (!isnan(x)) vm.wcs_offset[0] = vm.m_pos[0] - x;
            if (!isnan(y)) vm.wcs_offset[1] = vm.m_pos[1] - y;
            if (!isnan(z)) vm.wcs_offset[2] = vm.m_pos[2] - z;
            LOGI(TAG, "G10 Updated WCS Offset: %.2f, %.2f, %.2f", vm.wcs_offset[0], vm.wcs_offset[1], vm.wcs_offset[2]);
        }
    }
    else if (strncmp(line, "G28", 3) == 0) {
        vm.homed[0] = true; vm.homed[1] = true; vm.homed[2] = true;
        vm.m_pos[0] = 0; vm.m_pos[1] = 0; vm.m_pos[2] = 0;
        send_log_message(stream, "Homing All Axes... Done.");
    }
    else if (strncmp(line, "T", 1) == 0) {
        // Tool change T0, T1
        // Parse T parameter, handling "T T{...}" format from pendant
        // For simplicity, just grab the first number found after 'T' if it's simple "T0"
        // or just ignore complex exp.
        // The pendant sends: T T{global.mosPTID} -> we can't easily parse variables here without a var table.
        // We will just assume tool 0 if T0 is seen, or ack it.
        send_log_message(stream, "Tool Change: OK");
    }
}

// --- Task Loop ---

static void rrf_sim_task(void *arg) {
    LOGI(TAG, "RRF Device Simulator Task Started");
    
    char line_buf[256];
    size_t line_pos = 0;

    // Wait a bit for system stability
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        RRFMachineSimStream* stream = get_rrf_sim_stream_instance();
        
        if (stream) {
            int avail = stream->sim_available();
            if (avail > 0) {
                // Read in chunks
                uint8_t chunk[64];
                size_t read_len = stream->sim_read(chunk, sizeof(chunk));
                
                for(size_t i=0; i<read_len; i++) {
                    char c = (char)chunk[i];
                    if (c == '\n' || c == '\r') {
                        if (line_pos > 0) {
                            line_buf[line_pos] = '\0';
                            process_line(stream, line_buf);
                            line_pos = 0;
                        }
                    } else if (line_pos < sizeof(line_buf) - 1) {
                        line_buf[line_pos++] = c;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Polling interval
    }
}

void rrf_device_simulator_start() {
    TaskHandle_t sim_handle = NULL;
    xTaskCreate(rrf_sim_task, "RRF_SIM_DEV", 4096 * 2, NULL, 1, &sim_handle);
    task_registry_register_handle(sim_handle, "RRF_SIM_DEV");
}

#endif
