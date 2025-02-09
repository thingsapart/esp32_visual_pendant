#ifndef MACHINE_RRF_HPP
#define MACHINE_RRF_HPP

#include <Arduino.h>
#include <map>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <ArduinoJson.h> // or your preferred JSON library
#include "machine_interface.hpp"
#include <lvgl.h> // Include lvgl header

// Define platform and features (copied from micropython)
#define platform "ESP32-S3" // Or whatever platform you're targeting
#define RUN_SIM false  // Important: Define this based on your environment

// Forward declarations (unnecessary in this case but kept for completeness)
class MachineRRF;

// ============================================================================
// Simulated UART and Platform Specific Includes
// ============================================================================

#if RUN_SIM
#include <sstream>
#include <map>

// Minimum-viable UART + RRF Machine Sim:
// Pretends to be RRF connected via UART.
// Interprets some gcodes in the most basic way, closely tied to
// MachineInterface/MachineRRF, to allow testing MachineRRF on platforms
// that don't support the UART class.
class UART {
public:
    UART(int pos, long baud, int tx = -1, int rx = -1, int rxbuf = 0);

    void begin(long baud) {} // dummy
    bool available ();
    void write(const std::string& gcodes);

    bool available();
    std::string readLine();
    std::string read();

private:
    std::string last;
    std::vector<std::string> lastArgs;
    std::vector<float> position;
    std::vector<bool> axesHomed;
    int wcs;
    float feedMultiplier;
    std::vector<std::vector<float>> wcsOffsets;
    long baudRate;

    void processGcode(const std::string& command, const std::vector<std::string>& args);
    std::string processResponse(const std::string& command, const std::vector<std::string>& args);
};

#else
#include <HardwareSerial.h>

// Use HardwareSerial for real UART communication
class UART : public HardwareSerial {
public:
    UART(int portNum, long baud, int txPin, int rxPin, int rxBufSize);

    std::string readLine();
};

#endif // RUN_SIM

// ============================================================================
// MachineRRF Class (Subclass of MachineInterface)
// ============================================================================

class MachineRRF : public MachineInterface {
public:
    // Constants
    static const std::map<char, int> AXIS_NAMES;
    static const std::map<char, int> RRF_TO_STATUS;

    // Constructor
    MachineRRF(int sleepMs = MachineInterface::DEFAULT_SLEEP_MS);

    // Overridden methods from MachineInterface
    void _sendGcode(const std::string& gcode) override;
    bool _hasResponse() override;
    std::string _readResponse() override;
    void _updateMachineState(int pollState) override;

    void listFiles(const std::string& path) override;
    void runMacro(const std::string& macroName) override;
    void startJob(const std::string& jobName) override;
    bool isConnected() override;

protected:
    // Helper methods
    void _procMachineState(const std::string& cmd);
    void _updateNetworkInfo();
    void _updateFeedMultiplier();
    void _updateCurrentJob();
    void _updateMessageBox();
    void _updateEndstops();
    void _updateProbeVals();
    void _updateWcs();
    void _updateSpindles();
    void _updateTools();

    void parseMoveAxesBrief(const JsonArray& res);
    void parseMoveAxes(const JsonArray& res);
    void parseMoveAxesExt(const JsonArray& res);
    void parseM20Response(const JsonObject& jsonResp);
    void parseM409Response(const JsonObject& j);
    void parseJsonResponse(const std::string& jsonResp);
    void parseM408(const std::string& json_resp);

    void _continuousStop() override;
    void _continuousMove(const std::string& axis, float feed, int direction) override;

private:
    UART uart;
    bool connected;
    std::string input_sel;
    int input_idx;
};

#endif // MACHINE_RRF_HPP