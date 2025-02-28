// Machine/RRF config.

// Rate of polling state to gcode-sending, e.g. 10 means state is only polled every 10th
// time the GCode send interval function is called. 
// MACHINE_POLL_EVERY_NTH_PROCESS * MACHINE_SEND_GCODE_INTERVAL_MS should work out to about
// 100-200ms.
#define MACHINE_POLL_EVERY_NTH_INTERVAL 2
#define MACHINE_SEND_GCODE_INTERVAL_MS 50

// #define RRF_SIM 1
#define RRF_SERIAL_UART_NUM 0

// TOUCH and ENCODER debugging.
#define DEBUG_TOUCH 0
#define DEBUG_ENCODER 1