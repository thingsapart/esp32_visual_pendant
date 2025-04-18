// mock_rrf_serial.cpp

#include "rrf_machine_sim_stream.h"

#ifdef RRF_SIM

#include <map>
#include <mutex>
#include <cstring> // For strncpy, strcmp, strtok_r, strlen
#include <cstdlib> // For atof, atoi
#include <cstdio>  // For snprintf
#include <algorithm> // For std::min
#include <cctype>    // For isspace
#include <string>    // Make sure std::string is included
#include <vector>    // Make sure std::vector is included

#include "debug.h"

#define SILENCE_DEBUG

#ifdef SILENCE_DEBUG
#undef _d
#undef _df
#define _d(c, s) do {} while(0)
#define _df(c, s, ...) do {} while(0)
#endif

static const char *TAG = "rrf_machine_sim_stream";

// Assuming serial_instances is defined elsewhere and compatible
extern std::map<int, Stream *> serial_instances; // to insert.

// Helper function to replace all occurrences of a substring
void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

// Helper function to trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// Helper function to trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// Helper function to trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}


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
    wcs_ = 0; // Default to G54 (index 1, but stored as 0 internally?) - RRF default is G54 which is index 1. Let's set to 1. M409 reports 1-based index. G53 selects 0.
    feed_multiplier_ = 1.0f;
    relative = false;
    last_args_.reserve(10); // Pre-allocate some space
}

RRFMachineSimStream::~RRFMachineSimStream() {
    // Destructor (clean up if needed)
}

int RRFMachineSimStream::available() {
    return output_buffer_.length();
}

int RRFMachineSimStream::read() {
    if (output_buffer_.empty()) {
        return -1; // No data available
    }
    char c = output_buffer_.front();
    output_buffer_.erase(0, 1); // Remove the first character
    // _df(0, "READ: %c => %s", c, output_buffer_.c_str());
    return c;
}

// Using std::min from <algorithm>
size_t RRFMachineSimStream::readBytes(char *buffer, size_t length) {
    // _d(0, "READB");
    size_t len = std::min(output_buffer_.length(), length);
    if (len > 0) {
        memcpy(buffer, output_buffer_.data(), len); // Use memcpy for potentially binary data
        output_buffer_.erase(0, len); // Erase the read bytes
    }
    return len;
}

size_t RRFMachineSimStream::readBytesUntil(char terminator, char *buffer, size_t length) {
    // _d(0, "BYTESUNTIL");
    size_t n = output_buffer_.find(terminator);
    size_t len_to_read = 0;

    if (n == std::string::npos) { // Terminator not found
         len_to_read = std::min(output_buffer_.length(), length); // Read up to length or available data
         if (len_to_read > 0) {
            memcpy(buffer, output_buffer_.data(), len_to_read);
            output_buffer_.erase(0, len_to_read);
         }
         // Arduino Stream::readBytesUntil returns 0 if terminator not found within length.
         // This implementation reads data even if terminator is not found, matching Serial.readBytesUntil behavior better?
         // Let's stick to the interface: return 0 if terminator not found within buffer length limit 'length'.
         // Check if terminator exists *at all* first.
         if (output_buffer_.find(terminator) == std::string::npos) {
             return 0; // Mimic Arduino: Terminator not found, return 0 bytes read.
         }
         // If terminator exists *after* length, we should read up to length? No, the spec says read *until* terminator.
         // Let's re-read the Arduino spec. It reads until terminator OR length is reached.
         // If terminator is found: read up to terminator or length, whichever comes first.
         n = output_buffer_.find(terminator); // Find it again for clarity
         len_to_read = std::min(n, length);
         memcpy(buffer, output_buffer_.data(), len_to_read);
         if (len_to_read == length) { // Buffer full before terminator
             // Don't add null terminator, readBytes doesn't guarantee it.
             output_buffer_.erase(0, len_to_read); // Consume what was read
             return len_to_read;
         } else { // Terminator found within length
             buffer[len_to_read] = '\0'; // Add null terminator as per original code
             output_buffer_.erase(0, n + 1); // Consume up to and including terminator
             return len_to_read;
         }

    } else { // Terminator found at index n
        len_to_read = std::min(n, length -1); // Read up to terminator or length-1 to leave space for null
        if (len_to_read > 0) {
             memcpy(buffer, output_buffer_.data(), len_to_read);
        }
        buffer[len_to_read] = '\0'; // Null terminate
        output_buffer_.erase(0, n + 1); // Consume data including terminator
        return len_to_read;
    }
}


std::string RRFMachineSimStream::readStringUntil(const char terminator) {
    // _d(0, "STRINGUNTIL");
    size_t n = output_buffer_.find(terminator);
    if (n == std::string::npos) {
         // Terminator not found. Arduino String would return the whole buffer content and empty it.
         // Let's mimic that. Return empty string if timeout occurs (not applicable here) or buffer empty.
         // If buffer not empty but no terminator, Arduino waits for timeout. Simulating no timeout, return ""?
         // Or return current buffer? Let's return empty string for consistency with timeout case.
        return "";
    }

    std::string result = output_buffer_.substr(0, n);
    output_buffer_.erase(0, n + 1); // Consume data including terminator
    return result;
}

int RRFMachineSimStream::peek() {
    if (output_buffer_.empty()) {
        return -1; // No data available
    }
    return output_buffer_.front(); // Return the first character without removing
}


// write() methods are for receiving from the "host" (computer).
size_t RRFMachineSimStream::write(uint8_t byte) {
   input_buffer_ += static_cast<char>(byte);

    // Process G-code when a newline is received
    if (byte == '\n') {
        // Make a copy to process, in case process_gcode clears input_buffer_ or yields
        std::string line_to_process = input_buffer_;
        input_buffer_.clear(); // Clear the input buffer immediately
        // Unlock mutex before calling process_gcode which might call write again (via generate_response -> output_buffer_ -> read -> ...)
        // or block for a while. This requires process_gcode and its callees to be thread-safe regarding internal state.
        // Let's assume process_gcode is reasonably fast and doesn't cause deadlock. Keep lock for now.
        process_gcode(line_to_process.c_str());
    }
    return 1; //  return number of bytes written
}

size_t RRFMachineSimStream::write(const uint8_t *buffer, size_t size) {
    // More efficient bulk write
    size_t processed_bytes = 0;
    const char* char_buffer = reinterpret_cast<const char*>(buffer);
    size_t current_pos = 0;
    while (current_pos < size) {
        const char* newline_pos = static_cast<const char*>(memchr(char_buffer + current_pos, '\n', size - current_pos));
        size_t chunk_len;
        bool found_newline = (newline_pos != nullptr);

        if (found_newline) {
            chunk_len = (newline_pos - (char_buffer + current_pos)) + 1;
        } else {
            chunk_len = size - current_pos;
        }

        input_buffer_.append(char_buffer + current_pos, chunk_len);
        processed_bytes += chunk_len;

        if (found_newline) {
            std::string line_to_process = input_buffer_;
            input_buffer_.clear();
             // Process the complete line
             // Consider unlocking mutex here if process_gcode is slow or might re-enter write
            process_gcode(line_to_process.c_str());
        }
         current_pos += chunk_len;
    }
    return processed_bytes; // Return total bytes processed
}


void RRFMachineSimStream::flush() {
    // Nothing to do here for the simulator (no actual hardware buffer to flush TO)
    // If flush means "wait until all written data is sent", it's also N/A here.
}


void RRFMachineSimStream::process_gcode(const char *gcode_line) {
    char line[256]; // Buffer for strtok_r
    strncpy(line, gcode_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0'; // Ensure null termination.

    // Trim leading/trailing whitespace from the C-style string copy
    char *start = line;
    while (std::isspace(static_cast<unsigned char>(*start))) start++;
    if (*start == 0) return; // empty line

    char *end = start + strlen(start) - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(*end))) end--;
    *(end + 1) = '\0'; // Null-terminate trimmed string

    if (strlen(start) == 0) return;

    char *saveptr = NULL;
    char *command = strtok_r(start, " ", &saveptr); // get first token
    if (!command) return;

    last_command_ = command; // Store command as std::string
    last_args_.clear(); // Clear previous arguments

    char *token = strtok_r(NULL, " ", &saveptr); // Start parsing arguments
    while (token != NULL) {
        last_args_.push_back(token); // Store arguments in vector
        token = strtok_r(NULL, " ", &saveptr);
    }
    // At this point you have last_command_ (std::string) and the arguments
    // in last_args_ (std::vector<std::string>).

    // generate response needs the vector now
    generate_response(last_command_.c_str(), last_args_);

    // process command needs std::string
    process_last_command(last_command_);
}

void RRFMachineSimStream::process_last_command(std::string command_str) {
    // command_str is already a std::string, trim it if needed (process_gcode trimming might be sufficient)
    // trim(command_str); // Already trimmed in process_gcode before tokenization
    _df(0, "PROCESSING... %s, %d", command_str.c_str(), command_str == "G91");

    // Make a copy of args because recursive calls will modify last_args_
    std::vector<std::string> current_args = last_args_;

    if (command_str == "G28") {
         // Simple homing simulation:  set all axes to homed and position to 0.
        for (int i = 0; i < 3; i++) {
            axes_homed_[i] = true;
            pos_[i] = 0.0f;
        }
    }  else if (command_str == "G53") {
        wcs_ = 0; // Machine Coordinate System (Index 0 internal)
    }  else if (command_str == "G54") {
        wcs_ = 1;
    } else if (command_str == "G55") {
        wcs_ = 2;
    } else if (command_str == "G56") {
        wcs_ = 3;
    } else if (command_str == "G57") {
        wcs_ = 4;
    } else if (command_str == "G58") {
        wcs_ = 5;
    } else if (command_str == "G59") {
         wcs_ = 6; // G59
    } else if (command_str == "G59.1") {
        wcs_ = 7;
    } else if (command_str == "G59.2") {
        wcs_ = 8;
    } else if (command_str == "G59.3") {
        wcs_ = 9;
    } else if (command_str == "G10") {
      // G10 L2 Px Xx Yy Zz (Set WCS offsets - assume L2 for now)
      // G10 L20 Px Xx Yy Zz (Set WCS offsets - RRF uses L20)
        int wcs_index = -1;
        bool l20_found = false;
        for (const auto& arg : current_args) {
             if (!arg.empty()) {
                 if (arg[0] == 'L' && atof(arg.c_str() + 1) == 20) {
                     l20_found = true;
                 } else if (arg[0] == 'P') {
                    wcs_index = atoi(arg.c_str() + 1); // Duet WCS are 1-indexed
                    if (wcs_index < 1 || wcs_index > 10) { // Check 1-10 range
                        LOGE(TAG, "Invalid WCS index in G10 P: %s\n", arg.c_str());
                        return;
                    }
                    wcs_index--; // Convert to 0-based index for array
                 }
             }
        }

        if (l20_found && wcs_index != -1)
        {
              // apply the offset relative to the current *machine* position
            for (const auto& arg : current_args)
            {
                if (!arg.empty()) {
                    const char* token_str = arg.c_str();
                    if (token_str[0] == 'X') {
                        // WCS offset = Machine Position - User Position
                        // Here, G10 gives the desired User Position.
                        // So, Offset = Machine Position - Given User Position
                        wcs_offsets_[wcs_index][0] = pos_[0] - atof(token_str + 1);
                    } else if (token_str[0] == 'Y') {
                        wcs_offsets_[wcs_index][1] = pos_[1] - atof(token_str + 1);
                    } else if (token_str[0] == 'Z') {
                        wcs_offsets_[wcs_index][2] = pos_[2] - atof(token_str + 1);
                    }
                }
            }
        } else {
            // Handle G10 L2 or other variants if needed, or report error
            if (!l20_found) LOGW(TAG, "G10 command requires L20 parameter for setting WCS");
            if (wcs_index == -1) LOGW(TAG, "G10 L20 command requires P parameter for WCS index");
        }
    } else if (command_str == "G1" || command_str == "G0") {
        float x = NAN, y = NAN, z = NAN; // Use NAN to track which axes were specified
        // Very simplified movement simulation.
        for (const auto& arg : current_args)
        {
            if (!arg.empty()) {
                const char* token_str = arg.c_str();
                if (token_str[0] == 'X') {
                    x = atof(token_str + 1);
                } else if (token_str[0] == 'Y') {
                    y = atof(token_str + 1);
                } else if (token_str[0] == 'Z') {
                   z = atof(token_str + 1);
                }
                // Ignore Feed rate (F) and other parameters for now
            }
        }

        if (!relative) { // Absolute mode (G90)
            // Only update axes that were specified in the command
            if (!isnan(x)) pos_[0] = x - (wcs_ == 0 ? 0 : wcs_offsets_[wcs_ -1][0]); // Target user pos -> convert to machine pos
            if (!isnan(y)) pos_[1] = y - (wcs_ == 0 ? 0 : wcs_offsets_[wcs_ -1][1]);
            if (!isnan(z)) pos_[2] = z - (wcs_ == 0 ? 0 : wcs_offsets_[wcs_ -1][2]);
        } else { // Relative mode (G91)
            // Add specified offset to current machine position
            if (!isnan(x)) pos_[0] += x;
            if (!isnan(y)) pos_[1] += y;
            if (!isnan(z)) pos_[2] += z;
        }
        _df(0, ">> G0/G1 %s (%s) => X:%.3f Y:%.3f Z:%.3f | Machine Pos: %.3f, %.3f, %.3f",
            command_str.c_str(), relative ? "Rel" : "Abs",
            isnan(x) ? NAN : x, isnan(y) ? NAN : y, isnan(z) ? NAN : z,
            pos_[0], pos_[1], pos_[2]);

    } else if (command_str == "G90") {
        _df(0, "G90 - Absolute Positioning");
        relative = false;
    } else if (command_str == "G91") {
        _df(0, "G91 - Relative Positioning");
        relative = true;
    } else if (command_str == "M120") {
        _df(0, "M120 - Stack GCode");
        // TODO: Implement stack if needed
    } else if (command_str == "M121") {
        _df(0, "M121 - Pop GCode");
        // TODO: Implement stack if needed
    }
    // Handle other G/M codes as needed

    // Process potential chained commands (e.g., G90 G0 X10) - Original code seemed to do this.
    // This recursive call logic might be flawed if arguments aren't commands.
    // A better parser would handle multiple G/M codes on one line properly.
    // Let's disable the recursive part for now as it seems fragile.
    /*
    for (size_t i = 0; i < current_args.size(); ++i) {
        // This logic assumes arguments might be commands, which is unusual for GCode.
        // Revisit if necessary. Original code cleared args destructively.
    }
    */
}

// Generates "canned" RRF responses, example. Uses std::vector<std::string> for args.
void RRFMachineSimStream::generate_response(const char* command, const std::vector<std::string>& args) {
    std::string response_str; // Build response here

    // Example: Generate a response for M409 K"move.axes"
    if (strcmp(command, "M409") == 0) {
        if (!args.empty())
        {
            std::string key_param = args[0]; // e.g., K"move.axes"
            // Find the key value itself
            size_t first_quote = key_param.find('"');
            size_t last_quote = key_param.rfind('"');
            std::string key;
            if (first_quote != std::string::npos && last_quote != std::string::npos && first_quote != last_quote) {
                 key = key_param.substr(first_quote + 1, last_quote - first_quote - 1);
            } else {
                 // Handle case K='move.axes' or just Kmove.axes (if allowed)
                 // Simplistic: assume value starts after K
                 if (!key_param.empty() && (key_param[0] == 'K' || key_param[0] == 'k')) {
                    key = key_param.substr(1);
                    // Trim potential quotes if they weren't handled above
                    replaceAll(key, "\"", "");
                    replaceAll(key, "'", "");
                 } else {
                    LOGW(TAG, "Could not parse key from M409 argument: %s\n", key_param.c_str());
                 }
            }


             if (!key.empty()) {
                 if (key == "move.axes" || key == "move.axes[]") {
                    // The full response is quite long. Use snprintf carefully.
                    char buffer[2048]; // Adjusted buffer size

                    // Calculate user positions based on current WCS
                    int current_wcs_index = (wcs_ == 0) ? -1 : (wcs_ - 1); // -1 if G53 active, else 0-8
                    float user_x = pos_[0] + ((current_wcs_index >= 0) ? wcs_offsets_[current_wcs_index][0] : 0.0f);
                    float user_y = pos_[1] + ((current_wcs_index >= 0) ? wcs_offsets_[current_wcs_index][1] : 0.0f);
                    float user_z = pos_[2] + ((current_wcs_index >= 0) ? wcs_offsets_[current_wcs_index][2] : 0.0f);

                    const char *xh = axes_homed_[0] ? "true" : "false";
                    const char *yh = axes_homed_[1] ? "true" : "false";
                    const char *zh = axes_homed_[2] ? "true" : "false";

                    // Format the JSON response - Note: Using snprintf with many floats can be tricky.
                    // Consider using a JSON library for robustness if complexity increases.
                    int written = snprintf(buffer, sizeof(buffer),
                        "{\"key\":\"move.axes[]\",\"flags\":\"\",\"result\":[{"
                        "\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.0\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":%.3f,\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,"
                        "\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{" // WCS offsets X[0..9]
                        "\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":%.3f,\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,"
                        "\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{" // WCS offsets Y[0..9]
                        "\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":%s,\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":%.3f,\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":%.3f,\"visible\":true,"
                        "\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}" // WCS offsets Z[0..9]
                        "],\"next\":0}\n",
                        xh, pos_[0], user_x, // X axis data
                        wcs_offsets_[0][0], wcs_offsets_[1][0], wcs_offsets_[2][0], wcs_offsets_[3][0], wcs_offsets_[4][0], wcs_offsets_[5][0], wcs_offsets_[6][0], wcs_offsets_[7][0], wcs_offsets_[8][0], wcs_offsets_[9][0],
                        yh, pos_[1], user_y, // Y axis data
                        wcs_offsets_[0][1], wcs_offsets_[1][1], wcs_offsets_[2][1], wcs_offsets_[3][1], wcs_offsets_[4][1], wcs_offsets_[5][1], wcs_offsets_[6][1], wcs_offsets_[7][1], wcs_offsets_[8][1], wcs_offsets_[9][1],
                        zh, pos_[2], user_z, // Z axis data
                        wcs_offsets_[0][2], wcs_offsets_[1][2], wcs_offsets_[2][2], wcs_offsets_[3][2], wcs_offsets_[4][2], wcs_offsets_[5][2], wcs_offsets_[6][2], wcs_offsets_[7][2], wcs_offsets_[8][2], wcs_offsets_[9][2]
                        );
                     if (written > 0 && written < sizeof(buffer)) {
                          response_str = buffer;
                     } else {
                          LOGE(TAG, "snprintf buffer overflow or error creating M409 move.axes response");
                          response_str = "{\"err\":\"Buffer overflow generating response\"}\n";
                     }


                } else if (key == "move.speedFactor") {
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":%.3f}\n", feed_multiplier_);
                    response_str = buffer;
                } else if (key == "move.workplaceNumber")
                {
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer),  "{\"key\":\"move.workplaceNumber\",\"flags\":\"\",\"result\":%d}\n", wcs_); // RRF reports the active system (0=machine, 1=G54 etc)
                    response_str = buffer;
                }
                 // Add more M409 responses...
                else {
                    // Unknown key, RRF might return an error or empty result
                     LOGW(TAG, "Unknown M409 key: %s\n", key.c_str());
                     char buffer[128];
                     snprintf(buffer, sizeof(buffer), "{\"key\":\"%s\",\"flags\":\"\",\"result\":null,\"err\":1}\n", key.c_str()); // Respond with null result and error flag
                     response_str = buffer;
                }
            } else {
                LOGW(TAG, "M409 command received without valid key\n");
                response_str = "{\"err\":\"Missing or invalid key parameter for M409\"}\n";
            }
        } else {
             LOGW(TAG, "M409 command received without arguments\n");
             response_str = "{\"err\":\"Missing arguments for M409\"}\n";
        }
    } else if (strcmp(command, "M20") == 0)
    {
        // simplified M20 S2 P"/gcodes"
         if (!args.empty())
        {
            std::string path = "/"; // Default path
            // Look for P parameter
             for(const auto& arg : args) {
                 if (!arg.empty() && (arg[0] == 'P' || arg[0] == 'p')) {
                     // Extract path value, handling quotes
                     size_t first_quote = arg.find('"');
                     size_t last_quote = arg.rfind('"');
                     if (first_quote != std::string::npos && last_quote != std::string::npos && first_quote != last_quote) {
                        path = arg.substr(first_quote + 1, last_quote - first_quote - 1);
                     } else {
                        path = arg.substr(1); // Assume P/path without quotes
                     }
                     break; // Found P, stop looking
                 }
             }

            // Provide canned responses for specific paths
            if (path == "/macros" || path == "/macros/") {
                response_str =  "{\"dir\":\"/macros/\",\"first\":0,\"files\":[\"probe_work.g\",\"free_z.g\",\"all_zero.g\",\"z_zero.g\",\"zero_workspace.g\",\"work_align_xy10mm.gcode\",\"move_free.g\",\"touch_probe_work.g\"],\"next\":0,\"err\":0}\n";
            } else if (path == "/gcodes" || path == "/gcodes/")
            {
                response_str = "{\"dir\":\"/gcodes/\",\"first\":0,\"files\":[\"Updown.gcode\",\"Updown1.gcode\",\"updown 6.1.gcode\",\"MiniNC Z Plate.gcode\",\"updown 6.1 5mm-adaptive.gcode\",\"updown 6.1 - adaptive 5mm, pocket 0.5mm-0.75mm, slot 0.5mm.gcode\",\"MiniNC Z Plate Contout Only.gcode\"],\"next\":0,\"err\":0}\n";
            } else {
                 // Simulate "directory not found" error
                 char buffer[256];
                 snprintf(buffer, sizeof(buffer), "{\"dir\":\"%s\",\"first\":0,\"files\":[],\"next\":0,\"err\":2}\n", path.c_str()); // err=2 (ENOENT)
                 response_str = buffer;
            }

        } else {
             // M20 without path argument - list root? RRF usually requires a path.
             response_str = "{\"dir\":\"/\",\"first\":0,\"files\":[\"/gcodes\",\"/macros\",\"/sys\"],\"next\":0,\"err\":0}\n"; // Example root listing
        }
    }
     // Add other command responses as needed...
     else {
        // For most G/M codes that don't explicitly return data, RRF just responds with "ok\n" after execution.
        // We might want to add "ok\n" only if no other JSON response was generated.
        // However, some commands like M409 *do* generate JSON and *don't* add "ok".
        // Let's assume only commands *not* handled above get a simple "ok".
        if (response_str.empty()) {
             // response_str = "ok\n"; // Suppress "ok" for now to match behavior of M409 etc.
        }
    }

    // Append the generated response (if any) to the output buffer
    if (!response_str.empty()) {
        output_buffer_ += response_str;
        _df(0, ">>> RESPONSE to %s: %s", command, response_str.c_str());
    } else {
         _df(0, ">>> NO RESPONSE generated for %s", command);
    }
}


// Function to add the mock serial instance (implementation depends on how serial_instances is managed)
void add_mock_rrf_serial() {
    // Example: Create and add instance for UART 0
    // Ensure thread-safety if serial_instances is accessed concurrently
    // std::lock_guard<std::mutex> lck(some_global_mutex); // If needed
    // if (serial_instances.find(0) == serial_instances.end()) {
    //     serial_instances[0] = new RRFMachineSimStream(0);
    //     _d(0,"Mock RRF Stream added for UART 0");
    // }
}


#endif // RRF_SIM