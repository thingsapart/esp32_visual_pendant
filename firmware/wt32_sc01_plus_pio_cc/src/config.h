// Machine/RRF config.

// Rate of polling state to gcode-sending, e.g. 10 means state is only polled
// every 10th time the GCode send interval function is called.
// MACHINE_POLL_EVERY_NTH_PROCESS * MACHINE_SEND_GCODE_INTERVAL_MS should work
// out to about 100-200ms.
#define MACHINE_POLL_EVERY_NTH_INTERVAL 2
#define MACHINE_SEND_GCODE_INTERVAL_MS 100

// --- G-code Queue ---
#define MAX_GCODE_STR_LEN 128

#ifdef POSIX
#define RRF_SIM 1
#else
// #  define RRF_SIM 1
#endif

#define RRF_SERIAL_UART_NUM 0
// Connect to real RRF controller, but simulate moves via "G92 <AXIS>NN" when
// motors are not connected to controller. #define CONTROLLER_BENCH_TEST

// TOUCH and ENCODER debugging.
#define DEBUG_TOUCH 0
#define DEBUG_ENCODER 1

#define ASYNC_RESPONSE_PROCESSING 1  // Use Tasks to process serial/esp32 state.
#define ASYNC_GCODE_SENDING 1        // Use Tasks to process serial/esp32 state.

// ESP32-specific settings:
#ifdef ESP32_HW

#define HUB_MAC_ADDR \
  { 0x24, 0xEC, 0x4A, 0x38, 0xF8, 0xE0 }

// Specific device only:
// #define DISPLAY_MAC_ADDR { 0x8C, 0xBF, 0xEA, 0x0E, 0xCF, 0x7C }

// Broadcast:
#define DISPLAY_MAC_ADDR \
  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

#define TASK_MACHINE_CORE 1             // Machine Task Core.
#define TASK_MACHINE_STATE_PROC_CORE 1  // Machine State Processing Task Core.

#endif