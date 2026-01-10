#include "machine/rrf_machine_sim_stream.h"

#ifdef RRF_SIM

#include <algorithm>
#include <cstring>
#include "debug.h"

#if defined(ESP32_HW)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

static RRFMachineSimStream* g_sim_instance = nullptr;

RRFMachineSimStream* get_rrf_sim_stream_instance() {
    return g_sim_instance;
}

RRFMachineSimStream::RRFMachineSimStream(int uart_num) : uart_num_(uart_num) {
    g_sim_instance = this;
#if defined(ESP32_HW)
    mutex_ = xSemaphoreCreateMutex();
#endif
    
    // Initialize Machine State
    for (int i = 0; i < 3; i++) {
        pos_[i] = 0.0f;
        axes_homed_[i] = false;
        for (int j = 0; j < 10; j++) {
            wcs_offsets_[j][i] = 0.0f;
        }
    }
    wcs_ = 0;
    feed_multiplier_ = 1.0f;
    relative = false;
}

RRFMachineSimStream::~RRFMachineSimStream() {
    g_sim_instance = nullptr;
#if defined(ESP32_HW)
    if(mutex_) vSemaphoreDelete((SemaphoreHandle_t)mutex_);
#endif
}

// --- Pendant Side (Standard Stream API) ---

int RRFMachineSimStream::available() {
#if defined(ESP32_HW)
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
#endif
    int len = output_buffer_.length();
#if defined(ESP32_HW)
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
#endif
    return len;
}

int RRFMachineSimStream::read() {
    int c = -1;
#if defined(ESP32_HW)
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
#endif
    if (!output_buffer_.empty()) {
        c = (unsigned char)output_buffer_[0];
        output_buffer_.erase(0, 1);
    }
#if defined(ESP32_HW)
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
#endif
    return c;
}

int RRFMachineSimStream::peek() {
#if defined(ESP32_HW)
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
#endif
    int c = -1;
    if (!output_buffer_.empty()) {
        c = (unsigned char)output_buffer_[0];
    }
#if defined(ESP32_HW)
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
#endif
    return c;
}

size_t RRFMachineSimStream::write(uint8_t byte) {
    return write(&byte, 1);
}

size_t RRFMachineSimStream::write(const uint8_t *buffer, size_t size) {
#if defined(ESP32_HW)
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
#endif
    
    std::string incoming((const char*)buffer, size);
    input_buffer_ += incoming;

    // Process line by line
    size_t pos;
    while ((pos = input_buffer_.find('\n')) != std::string::npos) {
        std::string line = input_buffer_.substr(0, pos);
        input_buffer_.erase(0, pos + 1);
        process_gcode(line.c_str());
    }

#if defined(ESP32_HW)
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
#endif
    return size;
}

void RRFMachineSimStream::flush() {}

// --- Simulator Internal Logic ---

void RRFMachineSimStream::process_gcode(const char* gcode_line) {
    std::string line(gcode_line);
    // Basic trimming
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (line.empty()) return;

    if (line.find("M409") == 0) {
        // Handle Object Model Query
        if (line.find("move.axes") != std::string::npos) {
            generate_response("move.axes", {"pos", "homed"});
        } else if (line.find("state.status") != std::string::npos) {
            generate_response("state.status", {"idle"});
        }
    } else if (line.find("G28") == 0) {
        for(int i=0; i<3; i++) axes_homed_[i] = true;
    }
    // Add more parsing logic as needed
}

void RRFMachineSimStream::generate_response(const char* key, const std::vector<std::string>& results) {
    char buf[128];
    std::string resp = "{\"key\":\"";
    resp += key;
    resp += "\",\"result\":";
    
    if (strcmp(key, "move.axes") == 0) {
        resp += "[";
        for (int i = 0; i < 3; i++) {
            snprintf(buf, sizeof(buf), "{\"machinePosition\":%.3f,\"homed\":%s}", 
                     pos_[i], axes_homed_[i] ? "true" : "false");
            resp += buf;
            if (i < 2) resp += ",";
        }
        resp += "]";
    } else {
        resp += "\"idle\"";
    }
    
    resp += "}\n";
    output_buffer_ += resp;
}

// --- Simulator Task Side ---

int RRFMachineSimStream::sim_available() {
    return available(); // Use same logic
}

size_t RRFMachineSimStream::sim_read(uint8_t* buf, size_t size) {
    // This is used by the simulator task to read what the pendant sent
    return 0; // The logic above processes immediately on write()
}

size_t RRFMachineSimStream::sim_write(const uint8_t* buf, size_t size) {
#if defined(ESP32_HW)
    xSemaphoreTake((SemaphoreHandle_t)mutex_, portMAX_DELAY);
#endif
    output_buffer_.append((const char*)buf, size);
#if defined(ESP32_HW)
    xSemaphoreGive((SemaphoreHandle_t)mutex_);
#endif
    return size;
}

std::string RRFMachineSimStream::readStringUntil(char terminator) { return ""; }
size_t RRFMachineSimStream::readBytesUntil(char terminator, char *buffer, size_t length) { return 0; }
size_t RRFMachineSimStream::readBytes(char *buffer, size_t length) { return 0; }

#endif
