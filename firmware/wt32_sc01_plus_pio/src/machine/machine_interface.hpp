#ifndef MACHINE_INTERFACE_HPP
#define MACHINE_INTERFACE_HPP

#include <Arduino.h>
#include <map>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// Define platform and features (copied from micropython)
#define platform "ESP32-S3" // Or whatever platform you're targeting
#define RUN_SIM false  // Important: Define this based on your environment

// Forward declarations
class MachineInterface; // Forward declaration

// ============================================================================
// Constants and Enums (Translated from Python `const` and Classes)
// ============================================================================

namespace PollState {
    extern const int MACHINE_POSITION;
    extern const int MACHINE_POSITION_EXT;
    extern const int SPINDLE;
    extern const int PROBES;
    extern const int TOOLS;
    extern const int MESSAGES_AND_DIALOGS;
    extern const int END_STOPS;
    extern const int NETWORK;
    extern const int JOB_STATUS;
    extern const int LIST_FILES;
    extern const int LIST_MACROS;

    bool hasState(int pollState, int val);
    std::vector<int> allStates();
}

namespace MachineStatus {
    extern const int INITIALIZING;
    extern const int FLASHING_FIRMWARE;
    extern const int EMERGENCY_HALTED;
    extern const int OFF;
    extern const int PAUSED_DEC;
    extern const int PAUSED_RESUME;
    extern const int PAUSED;
    extern const int SIMULATING;
    extern const int RUNNING;
    extern const int TOOL_CHANGING;
    extern const int BUSY;
    extern const int UNKNOWN;
}

// ============================================================================
// MachineInterface Class (Base Class)
// ============================================================================

class MachineInterface {
public:
    // Constants
    static const int DEFAULT_POLL_STATES;
    static const int DEFAULT_SLEEP_MS;
    static const std::vector<std::string> AXES;
    static const int DEFAULT_FEED;
    static const int MAX_GCODE_Q_LEN;

    // Constructor
    MachineInterface(int sleepMs = DEFAULT_SLEEP_MS);

    // Callback management
    void addStateChangeCallback(std::function<void (MachineInterface*)> stateUpdateCallback);
    void addFilesChangedCb(std::function<void (MachineInterface*, const std::string&, const std::vector<std::string>&)> filesChangeCb, const std::string& path);
    void addPosChangedCb(std::function<void (MachineInterface*)> posChangeCb);
    void addHomeChangedCb(std::function<void (MachineInterface*)> homeChangeCb);
    void addWcsChangedCb(std::function<void (MachineInterface*)> wcsChangeCb);
    void addFeedChangedCb(std::function<void (MachineInterface*)> feedChangeCb);
    void addSensorsChangedCb(std::function<void (MachineInterface*)> sensorsChangeCb);
    void addDialogsChangedCb(std::function<void (MachineInterface*)> dialogsChangeCb);
    void addSpindlesToolsChangedCb(std::function<void (MachineInterface*)> spindlesToolsChangeCb);
    void addConnectedCb(std::function<void (MachineInterface*)> connCb);

    // State management
    bool isHomed(const std::vector<int>& axes = {});
    void sendGcode(const std::string& gcode, int pollState);

    void processGcodeQ(int max = 1);
    int nextPollState();
    void taskLoopIter();

    // Virtual update method (to be overridden by subclasses)
    virtual void _updateMachineState(int pollState) = 0;

    // Movement Functions
    void maybeExecuteContinuousMove();
    void positionUpdated();
    void homeUpdated();
    void wcsUpdated();
    void feedUpdated();
    void sensorsUpdated();
    void dialogsUpdated();
    void spindlesToolsUpdated();
    void filesUpdated(const std::string& fdir);
    void connectedUpdated();

    void updatePosition(const std::vector<float>& values, const std::vector<float>& valuesWcs);
    bool isContinuousMove();
    void moveContinuous(const std::string& axis, float feed, int direction);
    void moveContinuousStop();
    void move(const std::string& axis, float feed, float value);
    void __moveWithContinues(const std::string& axis, float feed, float value);
    void homeAll();
    void home(const std::string& axes);
    void home(const std::vector<std::string>& axes);
    std::string getWcsStr(int wcsOffs = -1);
    void setWcs(int wcs);
    void setWcsZero(int wcs, const std::vector<std::string>& axes);
    void nextWcs();

    // Abstract methods
    virtual void listFiles(const std::string& path) = 0;
    virtual void runMacro(const std::string& macroName) = 0;
    virtual void startJob(const std::string& jobName) = 0;
    virtual bool isConnected() = 0;

    // Debug print
    std::string debugPrint();

    int machine_status;
    std::vector<bool> axes_homed;
    std::vector<float> position;
    std::vector<float> wcs_position;
    std::vector<float> target_position;
    std::vector<float> moving_target_position;
    int wcs;

protected:
    // Helper function to convert axis name to index
    size_t _axisIdx(const std::string& ax);

    virtual void _continuousStop() = 0;
    virtual void _continuousMove(const std::string& axis, float feed, int direction) = 0;

    virtual void _sendGcode(const std::string& gcode) = 0;
    virtual bool _hasResponse() = 0;
    virtual std::string _readResponse() = 0;

protected:
    int sleep_ms;
    std::string tool;
    float z_offs;
    std::vector<float> probes;
    std::vector<std::string> end_stops; // Update this based on your needs
    std::vector<std::string> spindles;  // Update this based on your needs
    std::vector<std::string> dialogs; // Update this based on your needs
    int feed;
    int feed_req;
    float feed_multiplier;
    bool move_relative;
    bool move_step;
    std::vector<std::string> network;   // Update this based on your needs
    std::map<std::string, std::vector<std::string>> files;
    std::map<std::string, std::string> job; // Update this based on your needs
    std::map<std::string, std::string> message_box;   // Update this based on your needs
    std::deque<std::string> gcode_queue;
    int poll_state;
    std::vector<std::function<void (MachineInterface*)>> state_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> pos_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> home_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> wcs_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> feed_changes_cbs;
    std::vector<std::function<void (MachineInterface*)>> sensors_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> dialogs_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> spindles_tools_changed_cbs;
    std::vector<std::function<void (MachineInterface*)>> connected_cbs;
    std::map<std::string, std::vector<std::function<void (MachineInterface*, const std::string&, const std::vector<std::string>&)>>> files_changed_cbs;
    int polli;
    unsigned long last_continuous_tick;

    void _moveTo(const std::string& axis, float feed, float value, bool relative);
};
// Initialize the static member AXES
extern const std::vector<std::string> AXES_INIT;

void updateState(MachineInterface* m);

#if 0
class UART : HardwareSerial {
public:
    UART(int portNum, long baud, int txPin, int rxPin, int rxBufSize);
    std::string readLine();
};
#endif

#endif