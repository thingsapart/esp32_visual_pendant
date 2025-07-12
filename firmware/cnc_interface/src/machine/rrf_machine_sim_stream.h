// mock_rrf_serial.h
#ifndef RRF_MACHINE_SIM_STREAM_H__
#define RRF_MACHINE_SIM_STREAM_H__

#include "config.h"

#ifdef RRF_SIM

#include <cstddef>  // For size_t
#include <cstdint>  // For uint8_t
#include <string>
#include <vector>

// Forward declaration if Stream is defined elsewhere when ARDUINO is defined
#if defined(ARDUINO) && ARDUINO >= 100
#include <Stream.h>

#include "Arduino.h"  // Include Arduino specific headers if needed for Stream base class
#endif

#include "debug.h"  // Assuming debug.h is available

#if defined(ARDUINO) && ARDUINO >= 100
class RRFMachineSimStream : public Stream {
#else
// Define dummy Stream methods if not inheriting from Arduino Stream
// Or just define the class without inheritance if Stream methods are not needed
// outside Arduino context or are fully implemented here.
// Define override for non-C++11 compilers if needed
#ifndef override
#define override
#endif

class RRFMachineSimStream {
 public:
/*
    // Provide dummy implementations or stubs if needed when not inheriting
   Stream virtual int available() { return 0; } virtual int read() { return -1;
   } virtual int peek() { return -1; } virtual size_t write(uint8_t byte) {
   return 0; } virtual size_t write(const uint8_t *buffer, size_t size) { return
   0; } virtual void flush() {} virtual int availableForWrite() { return 0; } //
   Provide a reasonable default or 0
*/
#endif

 public:
  RRFMachineSimStream(int uart_num);
  virtual ~RRFMachineSimStream();  // Make destructor virtual if inheriting

  // Stream methods (potentially overriding Stream)
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  void flush() override;
  int availableForWrite() override { return 256; }  // large buffer

  // std::string equivalents
  std::string readStringUntil(const char terminator);
  size_t readBytesUntil(
      char terminator, char *buffer,
      size_t length);  // as readBytes with terminator character
  size_t readBytes(char *buffer,
                   size_t length);  // read chars from stream into buffer

  // Add any custom methods you need for your simulation here.
  void process_gcode(const char *gcode);
  // Modified signature to use std::vector<std::string>
  void generate_response(const char *command,
                         const std::vector<std::string> &args);
  // Modified signature to use std::string
  void process_last_command(std::string last_command);

 private:
  int uart_num_;               // Store the UART number.
  std::string input_buffer_;   // Buffer for incoming data (from "host")
  std::string output_buffer_;  // Buffer for outgoing data (to "host")

  // Internal RRF state (expand as needed)
  float pos_[3];
  bool axes_homed_[3];
  int wcs_;
  float feed_multiplier_;
  float wcs_offsets_[10][3];

  std::string last_command_;
  std::vector<std::string> last_args_;  // Use vector for dynamic arguments

  bool relative;
};

// define to insert mock into serial list:
extern void add_mock_rrf_serial();

// Cleanup override macro definition if we defined it
#if !defined(ARDUINO) || ARDUINO < 100
#ifdef override
#undef override
#endif
#endif

// Define Stream to be RRFMachineSimStream outside of Arduino context if needed
// Be careful with this, might conflict if Stream is used elsewhere
#ifndef ARDUINO
typedef RRFMachineSimStream Stream;
#endif

#endif  // RRF_SIM

#endif  // RRF_MACHINE_SIM_STREAM_H__