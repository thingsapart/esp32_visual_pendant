// mock_rrf_serial.h
#ifndef RRF_MACHINE_SIM_STREAM_H__
#define RRF_MACHINE_SIM_STREAM_H__

#include <string>
#include <vector>
#if defined(ARDUINO) && ARDUINO >= 100

#include "Arduino.h"
#include <Stream.h>

#include "debug.h"

class RRFMachineSimStream : public Stream {
#else
#define override

class RRFMachineSimStream {
#endif

#if 0

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
    std::string readStringUntil(const char terminator);
    size_t readBytesUntil(char terminator, char *buffer, size_t length);  // as readBytes with terminator character
    size_t readBytes(char *buffer, size_t length);  // read chars from stream into buffer

    // Add any custom methods you need for your simulation here.
    void process_gcode(const char *gcode);
    void generate_response(const char* command, const char** args, int num_args);
    void process_last_command(std::string last_command_);

private:
    int uart_num_; // Store the UART number.
    std::string input_buffer_; // Buffer for incoming data (from "host")
    std::string output_buffer_; // Buffer for outgoing data (to "host")

    // Internal RRF state (expand as needed)
    float pos_[3];
    bool axes_homed_[3];
    int wcs_;
    float feed_multiplier_;
    float wcs_offsets_[10][3];

    std::string last_command_;
    std::vector<std::string> last_args_; // store arguments, assume max 10 args. No longer fixed size!
    int num_last_args_;

    bool relative;
};

// define to insert mock into serial list:
extern void add_mock_rrf_serial();

#else

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
  String readStringUntil(const char terminator);
  size_t
  readBytesUntil(char terminator, char *buffer,
                 size_t length); // as readBytes with terminator character
  size_t readBytes(char *buffer,
                   size_t length); // read chars from stream into buffer

  // Add any custom methods you need for your simulation here.
  void process_gcode(const char *gcode);
  void generate_response(const char *command, const char **args, int num_args);
  void process_last_command(String last_command_);

private:
  int uart_num_;         // Store the UART number.
  String input_buffer_;  // Buffer for incoming data (from "host")
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

  bool relative;
};

// define to insert mock into serial list:
extern void add_mock_rrf_serial();
#endif

#if defined(override)
#undef override
#endif

#endif // RRF_MACHINE_SIM_STREAM_H__
