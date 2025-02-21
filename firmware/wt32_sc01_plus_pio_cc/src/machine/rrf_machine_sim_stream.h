// mock_rrf_serial.h
#ifndef RRF_MACHINE_SIM_STREAM_H__
#define RRF_MACHINE_SIM_STREAM_H__

#include "Arduino.h" // Needed for Stream

class RRFMachineSimStream : public Stream {
public:
    RRFMachineSimStream(int uart_num);
    ~RRFMachineSimStream();

    // Stream methods
    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t byte) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void flush() override;
    int availableForWrite() override { return 256; } //  large buffer

    // Add any custom methods you need for your simulation here.
    void process_gcode(const char *gcode);
    void generate_response(const char* command, const char** args, int num_args);

private:
    int uart_num_; // Store the UART number.
    String input_buffer_; // Buffer for incoming data (from "host")
    String output_buffer_; // Buffer for outgoing data (to "host")

    // Internal RRF state (expand as needed)
    float pos_[3];
    bool axes_homed_[3];
    int wcs_;
    float feed_multiplier_;
    float wcs_offsets_[10][3];

    String last_command_;
    String last_args_[10]; // store arguments, assume max 10 args.
    int num_last_args_;
};

// define to insert mock into serial list:
extern void add_mock_rrf_serial();

#endif // RRF_MACHINE_SIM_STREAM_H__
