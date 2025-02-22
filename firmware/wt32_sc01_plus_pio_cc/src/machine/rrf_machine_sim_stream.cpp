// mock_rrf_serial.cpp
#include "rrf_machine_sim_stream.h"
#include <map>
#include <mutex>

#include "debug.h"

#define SILENCE_DEBUG

#ifdef SILENCE_DEBUG
# undef _d
# undef _df
# define _d(c, s)
# define _df(c, s, ...)
#endif

extern std::map<int, Stream*> serial_instances; // to insert.

std::mutex serial_mtx;

#if 0
RRFMachineSimStream::RRFMachineSimStream(int uart_num) : uart_num_(uart_num) {
    _d(0, "INIT::RRFMachineSimStream");

    // Initialize internal state
    for (int i = 0; i < 3; i++) {
        pos_[i] = 0.0f;
        axes_homed_[i] = false;
         for (int j = 0; j < 10; j++) {
            wcs_offsets_[j][i] = 0.0f; // Initialize all WCS offsets to 0
         }
    }
    wcs_ = 0;
    feed_multiplier_ = 1.0f;
    num_last_args_ = 0;
    relative = false;
}

RRFMachineSimStream::~RRFMachineSimStream() {
    // Destructor (clean up if needed)
}

int RRFMachineSimStream::available() {
    return output_buffer_.length();
}

int RRFMachineSimStream::read() {
    std::lock_guard<std::mutex> lck(serial_mtx);

    if (output_buffer_.length() == 0) {
        return -1; // No data available
    }
    char c = output_buffer_[0];
    output_buffer_.remove(0, 1); // Remove the first character
    // _df(0, "READ: %c => %s", c, output_buffer_.c_str());
    return c;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

size_t RRFMachineSimStream::readBytes(char *buffer, size_t length) {
    std::lock_guard<std::mutex> lck(serial_mtx);
    // _d(0, "READB");
    size_t len = min(output_buffer_.length(), length);
    strncpy(buffer, output_buffer_.c_str(), len);
    return len;
}

size_t RRFMachineSimStream::readBytesUntil(char terminator, char *buffer, size_t length) {
    // _d(0, "BYTESUNTIL");
    std::lock_guard<std::mutex> lck(serial_mtx);
    int n = output_buffer_.indexOf(terminator);
    if (n < 0) { return 0; } // No terminator found

    size_t len = min(n, length);
    strncpy(buffer, output_buffer_.c_str(), len);
    buffer[len] = '\0';
    output_buffer_.remove(0, n+1);
    return len;
}

String RRFMachineSimStream::readStringUntil(const char terminator) {
    // _d(0, "STRINGUNTIL");
    int n = output_buffer_.indexOf(terminator);
    if (n < 0) { return ""; }

    char res[n + 1];
    strncpy(res, output_buffer_.c_str(), n);
    res[n] = '\0';
    output_buffer_.remove(0, n+1);
    return String(res); 
 }

int RRFMachineSimStream::peek() {
    if (output_buffer_.length() == 0) {
        return -1; // No data available
    }
    return output_buffer_[0]; // Return the first character without removing
}


// write() methods are for receiving from the "host" (computer).
size_t RRFMachineSimStream::write(uint8_t byte) {
   input_buffer_ += (char)byte;

    // Process G-code when a newline is received
    if (byte == '\n') {
        process_gcode(input_buffer_.c_str());
        input_buffer_ = ""; // Clear the input buffer
    }
    return 1; //  return number of bytes written
}

size_t RRFMachineSimStream::write(const uint8_t *buffer, size_t size) {
    size_t bytes_written = 0;
    for (size_t i = 0; i < size; i++)
    {
        bytes_written += write(buffer[i]);
    }
    return bytes_written;
}

void RRFMachineSimStream::flush() {
    // Nothing to do here for the simulator (no actual hardware buffer)
}


void RRFMachineSimStream::process_gcode(const char *gcode_line) {
    char line[256]; // Buffer for processing the line
    strncpy(line, gcode_line, sizeof(line) -1);
    line[sizeof(line) - 1] = '\0'; // Ensure null termination.

    size_t len = strlen(line);

    if (strlen(line) == 0) return;

    char *save = NULL;
    char *command = strtok_r(line, " ", &save); // get first token
    if (!command) return;

    last_command_ = command;
    num_last_args_ = 0;

    char *token = strtok_r(NULL, " ", &save); // Start parsing arguments from the rest of the line
    while (token != NULL && num_last_args_ < 10) {
        last_args_[num_last_args_++] = token;
        token = strtok_r(NULL, " ", &save);
    }
    // At this point you have last_command_ and the arguments
    // in last_args_ (up to num_last_args_).

    // generate response, so M409 works even without explicit prior commands.
    generate_response(last_command_.c_str(), (const char**) &last_args_, num_last_args_);

    process_last_command(last_command_);
}

void RRFMachineSimStream::process_last_command(String last_command_) {
    last_command_.trim();
    _df(0, "PROCESSING... %s, %d", last_command_.c_str(), last_command_ == "G91");

    if (last_command_ == "G28") {
         // Simple homing simulation:  set all axes to homed and position to 0.
        for (int i = 0; i < 3; i++) {
            axes_homed_[i] = true;
            pos_[i] = 0.0f;
        }

    }  else if (last_command_ == "G53") {
        wcs_ = 0; // Machine Coordinate System
    }  else if (last_command_ == "G54") {
        wcs_ = 1;
    } else if (last_command_ == "G55") {
        wcs_ = 2;
    } else if (last_command_ == "G56") {
        wcs_ = 3;
    } else if (last_command_ == "G57") {
        wcs_ = 4;
    } else if (last_command_ == "G58") {
        wcs_ = 5;
    } else if (last_command_ == "G59") {
         wcs_ = 6; // G59
    } else if (last_command_ == "G59.1") {
        wcs_ = 7;
    } else if (last_command_ == "G59.2") {
        wcs_ = 8;
    } else if (last_command_ == "G59.3") {
        wcs_ = 9;
    } else if (last_command_ == "G10") {
      // G10 L20 Px Xx Yy Zz  (Set WCS offsets)
        int wcs_index = -1;
        for (int i=0; i<num_last_args_; ++i)
        {
             const char* token = last_args_[i].c_str();
             if (token[0] == 'P') {
                wcs_index = atoi(token + 1) -1; //Duet WCS are 1-indexed, array is 0.
                if (wcs_index < 0 || wcs_index > 9) {
                    // Invalid WCS index
                    Serial.printf("Invalid WCS index in G10: %s\n", token);
                    return;
                }
             }
        }

        if (wcs_index != -1)
        {
              // apply the offset relative to the current position
            for (int i=0; i<num_last_args_; ++i)
            {
                const char* token = last_args_[i].c_str();
                if (token[0] == 'X') {
                    wcs_offsets_[wcs_index][0] =  atof(token + 1) - pos_[0];
                    last_args_[i] = "";
                } else if (token[0] == 'Y') {
                    wcs_offsets_[wcs_index][1] =  atof(token + 1) - pos_[1];
                    last_args_[i] = "";
                } else if (token[0] == 'Z') {
                    wcs_offsets_[wcs_index][2] =  atof(token + 1) - pos_[2];
                    last_args_[i] = "";
                }
            }
        }
    } else if (last_command_ == "G1" || last_command_ == "G0") {
        float x = 0.0, y = 0.0, z = 0.0;
        // Very simplified movement simulation.
        for (int i=0; i < num_last_args_; ++i)
        {
            const char* token = last_args_[i].c_str();
            if (token[0] == 'X') {
                x = atof(token + 1);
                last_args_[i] = "";
            } else if (token[0] == 'Y') {
                y = atof(token + 1);
                last_args_[i] = "";
            } else if (token[0] == 'Z') {
               z = atof(token + 1);
               last_args_[i] = "";
            }
        }
        if (!relative) {
            pos_[0] = x;
            pos_[1] = y;
            pos_[2] = z;
        } else {
            pos_[0] += x;
            pos_[1] += y;
            pos_[2] += z;
        }
        _df(0, ">> G0/G1 %s => %f %f %f (x,y,z) - %f, %f, %f (pos)", last_command_.c_str(), x, y, z, pos_[0], pos_[1], pos_[2]);
    } else if (last_command_ == "G90") {
        _df(0, "G90");
        relative = false;
    } else if (last_command_ == "G91") {
        _df(0, "G91");
        relative = true;
    } else if (last_command_ == "M120") {
        _df(0, "M120");
        // TODO, ignore for now.
    } else if (last_command_ == "M121") {
        _df(0, "M121");
        // TODO, ignore for now.
    }

    for (int i=0; i < num_last_args_; ++i) {
        if (last_args_[i].length() > 0) {
            last_command_ = last_args_[i];
            last_args_[i] = "";
            process_last_command(last_command_);
        }
    }
}

// Generates "canned" RRF responses, example.
void RRFMachineSimStream::generate_response(const char* command, const char** args, int num_args) {
    std::lock_guard<std::mutex> lck(serial_mtx);

    // Example: Generate a response for M409 K"move.axes"
    if (strcmp(command, "M409") == 0) {
        if (num_args > 0)
        {
            String key = args[0];
            key.remove(0, 1);
            key.replace("\"", "");
            key.replace("'", "");
             if (key == "move.axes" || key == "move.axes[]") {
                // The full response is quite long.
                char response[4096];

                 float x = pos_[0];
                float xwcs = wcs_offsets_[wcs_][0];
                const char *xh = axes_homed_[0] ? "true" : "false";
                float xu = x + xwcs;
                float y = pos_[1];
                const char *yh = axes_homed_[1] ? "true" : "false";
                float ywcs = wcs_offsets_[wcs_][1];
                float yu = y + ywcs;
                float z = pos_[2];
                const char *zh = axes_homed_[2] ? "true" : "false";
                float zwcs = wcs_offsets_[wcs_][2];
                float zu = z + zwcs;

                snprintf(response, sizeof(response),
                    "{\"key\":\"move.axes[]\",\"flags\":\"\",\"result\":[{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":%.3f,\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":%.3f,\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.3\"],\"homed\":%s,\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":%.3f,\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}],\"next\":0}\n",
                    xh, x, xu, wcs_offsets_[0][0],wcs_offsets_[1][0],wcs_offsets_[2][0],wcs_offsets_[3][0],wcs_offsets_[4][0],wcs_offsets_[5][0],wcs_offsets_[6][0],wcs_offsets_[7][0],wcs_offsets_[8][0],wcs_offsets_[9][0],
                    yh, y, yu, wcs_offsets_[0][1],wcs_offsets_[1][1],wcs_offsets_[2][1],wcs_offsets_[3][1],wcs_offsets_[4][1],wcs_offsets_[5][1],wcs_offsets_[6][1],wcs_offsets_[7][1],wcs_offsets_[8][1],wcs_offsets_[9][1],
                    zh, z, zu, wcs_offsets_[0][2],wcs_offsets_[1][2],wcs_offsets_[2][2],wcs_offsets_[3][2],wcs_offsets_[4][2],wcs_offsets_[5][2],wcs_offsets_[6][2],wcs_offsets_[7][2],wcs_offsets_[8][2],wcs_offsets_[9][2]
                    );

                output_buffer_ += response;
                //output_buffer_ += "ok\n"; // RRF usually responds with "ok"

            } else if (key == "move.speedFactor") {
                char response[64];
                snprintf(response, sizeof(response), "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":%f}\n", feed_multiplier_);
                output_buffer_ += response;
                // output_buffer_ += "ok\n";
            } else if (key == "move.workplaceNumber")
            {
                 char response[64];
                snprintf(response, sizeof(response),  "{\"key\":\"move.workplaceNumber\",\"flags\":\"\",\"result\":%d}\n", wcs_ +1); // RRF uses 1-indexed.
                output_buffer_ += response;
                // output_buffer_ += "ok\n";
            }
             // add more M409 responses.
            else {
                // Unknown key, you might want to log this or provide a default response
                 Serial.printf("Unknown M409 key: %s\n", key.c_str());
            }
        }
    } else if (strcmp(command, "M20")==0)
    {
        // simplified M20:
         if (num_args > 0)
        {
            String p_arg = args[0];
            if(p_arg.startsWith("P") == 0)
            {
                String path = p_arg.substring(1); // Remove "P"
                path.replace("\"", "");
                if (path == "/macros") {
                    output_buffer_ +=  "{\"dir\":\"/macros/\",\"first\":0,\"files\":[\"probe_work.g\",\"free_z.g\",\"all_zero.g\",\"z_zero.g\",\"zero_workspace.g\",\"work_align_xy10mm.gcode\",\"move_free.g\",\"touch_probe_work.g\"],\"next\":0,\"err\":0}\n";
                    // output_buffer_ += "ok\n";
                } else if (path == "/gcodes")
                {
                    output_buffer_ += "{\"dir\":\"/gcodes/\",\"first\":0,\"files\":[\"Updown.gcode\",\"Updown1.gcode\",\"updown 6.1.gcode\",\"MiniNC Z Plate.gcode\",\"updown 6.1 5mm-adaptive.gcode\",\"updown 6.1 - adaptive 5mm, pocket 0.5mm-0.75mm, slot 0.5mm.gcode\",\"MiniNC Z Plate Contout Only.gcode\"],\"next\":0,\"err\":0}\n";
                    // output_buffer_ += "ok\n";
                } else {
                     output_buffer_ += "{\"dir\":\"" + path + "\",\"err\":2}\n"; // err=2 is a common error code.
                     // output_buffer_ += "ok\n";
                }
            }
        }
    }
     else {
        // output_buffer_ += "ok\n"; // always add ok, even to unknown commands.
    }

    _df(0, " >>> RESPONSE: %s", output_buffer_.c_str());
}





#else 

RRFMachineSimStream::RRFMachineSimStream(int uart_num) : uart_num_(uart_num) {
    _d(0, "INIT::RRFMachineSimStream");

    // Initialize internal state
    for (int i = 0; i < 3; i++) {
        pos_[i] = 0.0f;
        axes_homed_[i] = false;
         for (int j = 0; j < 10; j++) {
            wcs_offsets_[j][i] = 0.0f; // Initialize all WCS offsets to 0
         }
    }
    wcs_ = 0;
    feed_multiplier_ = 1.0f;
    num_last_args_ = 0;
    relative = false;
}

RRFMachineSimStream::~RRFMachineSimStream() {
    // Destructor (clean up if needed)
}

int RRFMachineSimStream::available() {
    return output_buffer_.length();
}

int RRFMachineSimStream::read() {
    std::lock_guard<std::mutex> lck(serial_mtx);

    if (output_buffer_.length() == 0) {
        return -1; // No data available
    }
    char c = output_buffer_[0];
    output_buffer_.remove(0, 1); // Remove the first character
    // _df(0, "READ: %c => %s", c, output_buffer_.c_str());
    return c;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

size_t RRFMachineSimStream::readBytes(char *buffer, size_t length) {
    std::lock_guard<std::mutex> lck(serial_mtx);
    // _d(0, "READB");
    size_t len = min(output_buffer_.length(), length);
    strncpy(buffer, output_buffer_.c_str(), len);
    return len;
}

size_t RRFMachineSimStream::readBytesUntil(char terminator, char *buffer, size_t length) {
    // _d(0, "BYTESUNTIL");
    std::lock_guard<std::mutex> lck(serial_mtx);
    int n = output_buffer_.indexOf(terminator);
    if (n < 0) { return 0; } // No terminator found

    size_t len = min(n, length);
    strncpy(buffer, output_buffer_.c_str(), len);
    buffer[len] = '\0';
    output_buffer_.remove(0, n+1);
    return len;
}

String RRFMachineSimStream::readStringUntil(const char terminator) {
    // _d(0, "STRINGUNTIL");
    int n = output_buffer_.indexOf(terminator);
    if (n < 0) { return ""; }

    char res[n + 1];
    strncpy(res, output_buffer_.c_str(), n);
    res[n] = '\0';
    output_buffer_.remove(0, n+1);
    return String(res); 
 }

int RRFMachineSimStream::peek() {
    if (output_buffer_.length() == 0) {
        return -1; // No data available
    }
    return output_buffer_[0]; // Return the first character without removing
}


// write() methods are for receiving from the "host" (computer).
size_t RRFMachineSimStream::write(uint8_t byte) {
   input_buffer_ += (char)byte;

    // Process G-code when a newline is received
    if (byte == '\n') {
        process_gcode(input_buffer_.c_str());
        input_buffer_ = ""; // Clear the input buffer
    }
    return 1; //  return number of bytes written
}

size_t RRFMachineSimStream::write(const uint8_t *buffer, size_t size) {
    size_t bytes_written = 0;
    for (size_t i = 0; i < size; i++)
    {
        bytes_written += write(buffer[i]);
    }
    return bytes_written;
}

void RRFMachineSimStream::flush() {
    // Nothing to do here for the simulator (no actual hardware buffer)
}


void RRFMachineSimStream::process_gcode(const char *gcode_line) {
    char line[256]; // Buffer for processing the line
    strncpy(line, gcode_line, sizeof(line) -1);
    line[sizeof(line) - 1] = '\0'; // Ensure null termination.

    size_t len = strlen(line);

    if (strlen(line) == 0) return;

    char *save = NULL;
    char *command = strtok_r(line, " ", &save); // get first token
    if (!command) return;

    last_command_ = command;
    num_last_args_ = 0;

    char *token = strtok_r(NULL, " ", &save); // Start parsing arguments from the rest of the line
    while (token != NULL && num_last_args_ < 10) {
        last_args_[num_last_args_++] = token;
        token = strtok_r(NULL, " ", &save);
    }
    // At this point you have last_command_ and the arguments
    // in last_args_ (up to num_last_args_).

    // generate response, so M409 works even without explicit prior commands.
    generate_response(last_command_.c_str(), (const char**) &last_args_, num_last_args_);

    process_last_command(last_command_);
}

void RRFMachineSimStream::process_last_command(String last_command_) {
    last_command_.trim();
    _df(0, "PROCESSING... %s, %d", last_command_.c_str(), last_command_ == "G91");

    if (last_command_ == "G28") {
         // Simple homing simulation:  set all axes to homed and position to 0.
        for (int i = 0; i < 3; i++) {
            axes_homed_[i] = true;
            pos_[i] = 0.0f;
        }

    }  else if (last_command_ == "G53") {
        wcs_ = 0; // Machine Coordinate System
    }  else if (last_command_ == "G54") {
        wcs_ = 1;
    } else if (last_command_ == "G55") {
        wcs_ = 2;
    } else if (last_command_ == "G56") {
        wcs_ = 3;
    } else if (last_command_ == "G57") {
        wcs_ = 4;
    } else if (last_command_ == "G58") {
        wcs_ = 5;
    } else if (last_command_ == "G59") {
         wcs_ = 6; // G59
    } else if (last_command_ == "G59.1") {
        wcs_ = 7;
    } else if (last_command_ == "G59.2") {
        wcs_ = 8;
    } else if (last_command_ == "G59.3") {
        wcs_ = 9;
    } else if (last_command_ == "G10") {
      // G10 L20 Px Xx Yy Zz  (Set WCS offsets)
        int wcs_index = -1;
        for (int i=0; i<num_last_args_; ++i)
        {
             const char* token = last_args_[i].c_str();
             if (token[0] == 'P') {
                wcs_index = atoi(token + 1) -1; //Duet WCS are 1-indexed, array is 0.
                if (wcs_index < 0 || wcs_index > 9) {
                    // Invalid WCS index
                    Serial.printf("Invalid WCS index in G10: %s\n", token);
                    return;
                }
             }
        }

        if (wcs_index != -1)
        {
              // apply the offset relative to the current position
            for (int i=0; i<num_last_args_; ++i)
            {
                const char* token = last_args_[i].c_str();
                if (token[0] == 'X') {
                    wcs_offsets_[wcs_index][0] =  atof(token + 1) - pos_[0];
                    last_args_[i] = "";
                } else if (token[0] == 'Y') {
                    wcs_offsets_[wcs_index][1] =  atof(token + 1) - pos_[1];
                    last_args_[i] = "";
                } else if (token[0] == 'Z') {
                    wcs_offsets_[wcs_index][2] =  atof(token + 1) - pos_[2];
                    last_args_[i] = "";
                }
            }
        }
    } else if (last_command_ == "G1" || last_command_ == "G0") {
        float x = 0.0, y = 0.0, z = 0.0;
        // Very simplified movement simulation.
        for (int i=0; i < num_last_args_; ++i)
        {
            const char* token = last_args_[i].c_str();
            if (token[0] == 'X') {
                x = atof(token + 1);
                last_args_[i] = "";
            } else if (token[0] == 'Y') {
                y = atof(token + 1);
                last_args_[i] = "";
            } else if (token[0] == 'Z') {
               z = atof(token + 1);
               last_args_[i] = "";
            }
        }
        if (!relative) {
            pos_[0] = x;
            pos_[1] = y;
            pos_[2] = z;
        } else {
            pos_[0] += x;
            pos_[1] += y;
            pos_[2] += z;
        }
        _df(0, ">> G0/G1 %s => %f %f %f (x,y,z) - %f, %f, %f (pos)", last_command_.c_str(), x, y, z, pos_[0], pos_[1], pos_[2]);
    } else if (last_command_ == "G90") {
        _df(0, "G90");
        relative = false;
    } else if (last_command_ == "G91") {
        _df(0, "G91");
        relative = true;
    } else if (last_command_ == "M120") {
        _df(0, "M120");
        // TODO, ignore for now.
    } else if (last_command_ == "M121") {
        _df(0, "M121");
        // TODO, ignore for now.
    }

    for (int i=0; i < num_last_args_; ++i) {
        if (last_args_[i].length() > 0) {
            last_command_ = last_args_[i];
            last_args_[i] = "";
            process_last_command(last_command_);
        }
    }
}

// Generates "canned" RRF responses, example.
void RRFMachineSimStream::generate_response(const char* command, const char** args, int num_args) {
    std::lock_guard<std::mutex> lck(serial_mtx);

    // Example: Generate a response for M409 K"move.axes"
    if (strcmp(command, "M409") == 0) {
        if (num_args > 0)
        {
            String key = args[0];
            key.remove(0, 1);
            key.replace("\"", "");
            key.replace("'", "");
             if (key == "move.axes" || key == "move.axes[]") {
                // The full response is quite long.
                char response[4096];

                 float x = pos_[0];
                float xwcs = wcs_offsets_[wcs_][0];
                const char *xh = axes_homed_[0] ? "true" : "false";
                float xu = x + xwcs;
                float y = pos_[1];
                const char *yh = axes_homed_[1] ? "true" : "false";
                float ywcs = wcs_offsets_[wcs_][1];
                float yu = y + ywcs;
                float z = pos_[2];
                const char *zh = axes_homed_[2] ? "true" : "false";
                float zwcs = wcs_offsets_[wcs_][2];
                float zu = z + zwcs;

                snprintf(response, sizeof(response),
                    "{\"key\":\"move.axes[]\",\"flags\":\"\",\"result\":[{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":%.3f,\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":%.3f,\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.3\"],\"homed\":%s,\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":%.3f,\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}],\"next\":0}\n",
                    xh, x, xu, wcs_offsets_[0][0],wcs_offsets_[1][0],wcs_offsets_[2][0],wcs_offsets_[3][0],wcs_offsets_[4][0],wcs_offsets_[5][0],wcs_offsets_[6][0],wcs_offsets_[7][0],wcs_offsets_[8][0],wcs_offsets_[9][0],
                    yh, y, yu, wcs_offsets_[0][1],wcs_offsets_[1][1],wcs_offsets_[2][1],wcs_offsets_[3][1],wcs_offsets_[4][1],wcs_offsets_[5][1],wcs_offsets_[6][1],wcs_offsets_[7][1],wcs_offsets_[8][1],wcs_offsets_[9][1],
                    zh, z, zu, wcs_offsets_[0][2],wcs_offsets_[1][2],wcs_offsets_[2][2],wcs_offsets_[3][2],wcs_offsets_[4][2],wcs_offsets_[5][2],wcs_offsets_[6][2],wcs_offsets_[7][2],wcs_offsets_[8][2],wcs_offsets_[9][2]
                    );

                output_buffer_ += response;
                //output_buffer_ += "ok\n"; // RRF usually responds with "ok"

            } else if (key == "move.speedFactor") {
                char response[64];
                snprintf(response, sizeof(response), "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":%f}\n", feed_multiplier_);
                output_buffer_ += response;
                // output_buffer_ += "ok\n";
            } else if (key == "move.workplaceNumber")
            {
                 char response[64];
                snprintf(response, sizeof(response),  "{\"key\":\"move.workplaceNumber\",\"flags\":\"\",\"result\":%d}\n", wcs_ +1); // RRF uses 1-indexed.
                output_buffer_ += response;
                // output_buffer_ += "ok\n";
            }
             // add more M409 responses.
            else {
                // Unknown key, you might want to log this or provide a default response
                 Serial.printf("Unknown M409 key: %s\n", key.c_str());
            }
        }
    } else if (strcmp(command, "M20")==0)
    {
        // simplified M20:
         if (num_args > 0)
        {
            String p_arg = args[0];
            if(p_arg.startsWith("P") == 0)
            {
                String path = p_arg.substring(1); // Remove "P"
                path.replace("\"", "");
                if (path == "/macros") {
                    output_buffer_ +=  "{\"dir\":\"/macros/\",\"first\":0,\"files\":[\"probe_work.g\",\"free_z.g\",\"all_zero.g\",\"z_zero.g\",\"zero_workspace.g\",\"work_align_xy10mm.gcode\",\"move_free.g\",\"touch_probe_work.g\"],\"next\":0,\"err\":0}\n";
                    // output_buffer_ += "ok\n";
                } else if (path == "/gcodes")
                {
                    output_buffer_ += "{\"dir\":\"/gcodes/\",\"first\":0,\"files\":[\"Updown.gcode\",\"Updown1.gcode\",\"updown 6.1.gcode\",\"MiniNC Z Plate.gcode\",\"updown 6.1 5mm-adaptive.gcode\",\"updown 6.1 - adaptive 5mm, pocket 0.5mm-0.75mm, slot 0.5mm.gcode\",\"MiniNC Z Plate Contout Only.gcode\"],\"next\":0,\"err\":0}\n";
                    // output_buffer_ += "ok\n";
                } else {
                     output_buffer_ += "{\"dir\":\"" + path + "\",\"err\":2}\n"; // err=2 is a common error code.
                     // output_buffer_ += "ok\n";
                }
            }
        }
    }
     else {
        // output_buffer_ += "ok\n"; // always add ok, even to unknown commands.
    }

    _df(0, " >>> RESPONSE: %s", output_buffer_.c_str());
}
#endif
