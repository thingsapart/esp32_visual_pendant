#include "machine_interface.hpp"

// ============================================================================
// Constants and Enums (Translated from Python `const` and Classes)
// ============================================================================

namespace PollState {
    const int MACHINE_POSITION = 1;
    const int MACHINE_POSITION_EXT = 2;
    const int SPINDLE = 4;
    const int PROBES = 8;
    const int TOOLS = 16;
    const int MESSAGES_AND_DIALOGS = 32;
    const int END_STOPS = 64;
    const int NETWORK = 128;
    const int JOB_STATUS = 256;
    const int LIST_FILES = 512;
    const int LIST_MACROS = 1024;

    bool hasState(int pollState, int val) {
        return (pollState & val) != 0;
    }

    std::vector<int> allStates() {
        return {MACHINE_POSITION, SPINDLE, PROBES, TOOLS, MESSAGES_AND_DIALOGS, END_STOPS};
    }
}

namespace MachineStatus {
    const int INITIALIZING = 0;
    const int FLASHING_FIRMWARE = 1;
    const int EMERGENCY_HALTED = 2;
    const int OFF = 3;
    const int PAUSED_DEC = 4;
    const int PAUSED_RESUME = 5;
    const int PAUSED = 6;
    const int SIMULATING = 7;
    const int RUNNING = 8;
    const int TOOL_CHANGING = 9;
    const int BUSY = 10;
    const int UNKNOWN = 100;
}

// ============================================================================
// MachineInterface Class (Base Class)
// ============================================================================

const int MachineInterface::DEFAULT_POLL_STATES = PollState::MACHINE_POSITION | PollState::SPINDLE |
                                            PollState::PROBES | PollState::TOOLS |
                                            PollState::MESSAGES_AND_DIALOGS | PollState::END_STOPS;
const int MachineInterface::DEFAULT_SLEEP_MS = 200;
const std::vector<std::string> MachineInterface::AXES = {"X", "Y", "Z"};
const int MachineInterface::DEFAULT_FEED = 3000;
const int MachineInterface::MAX_GCODE_Q_LEN = 10;

// Constructor
MachineInterface::MachineInterface(int sleepMs) : sleep_ms(sleepMs) {
    machine_status = MachineStatus::UNKNOWN;
    axes_homed = {false, false, false};
    position = {0.0, 0.0, 0.0};
    wcs_position = {0.0, 0.0, 0.0};
    target_position = {0.0, 0.0, 0.0};
    moving_target_position = {0.0, 0.0, 0.0};
    wcs = 1;
    tool = "None";
    z_offs = 0.0;
    probes = {0.0, 0.0};
    end_stops = {};
    spindles = {};
    dialogs = {};
    feed = 0;
    feed_req = 0;
    feed_multiplier = 1.0;
    move_relative = false; // Default
    move_step = true; // Default
    network = {};
    files = {};
    job = {};
    message_box = {};
    poll_state = DEFAULT_POLL_STATES;
    polli = -1;
    last_continuous_tick = 0;
}

// Callback management
void MachineInterface::addStateChangeCallback(std::function<void (MachineInterface*)> stateUpdateCallback) {
    state_changed_cbs.push_back(stateUpdateCallback);
}

void MachineInterface::addFilesChangedCb(std::function<void (MachineInterface*, const std::string&, const std::vector<std::string>&)> filesChangeCb, const std::string& path) {
    if (files_changed_cbs.find(path) == files_changed_cbs.end()) {
        files_changed_cbs[path] = {};
    }
    files_changed_cbs[path].push_back(filesChangeCb);
}

void MachineInterface::addPosChangedCb(std::function<void (MachineInterface*)> posChangeCb) {
    pos_changed_cbs.push_back(posChangeCb);
}

void MachineInterface::addHomeChangedCb(std::function<void (MachineInterface*)> homeChangeCb) {
    home_changed_cbs.push_back(homeChangeCb);
}

void MachineInterface::addWcsChangedCb(std::function<void (MachineInterface*)> wcsChangeCb) {
    wcs_changed_cbs.push_back(wcsChangeCb);
}

void MachineInterface::addFeedChangedCb(std::function<void (MachineInterface*)> feedChangeCb) {
    feed_changes_cbs.push_back(feedChangeCb);
}

void MachineInterface::addSensorsChangedCb(std::function<void (MachineInterface*)> sensorsChangeCb) {
    sensors_changed_cbs.push_back(sensorsChangeCb);
}

void MachineInterface::addDialogsChangedCb(std::function<void (MachineInterface*)> dialogsChangeCb) {
    dialogs_changed_cbs.push_back(dialogsChangeCb);
}

void MachineInterface::addSpindlesToolsChangedCb(std::function<void (MachineInterface*)> spindlesToolsChangeCb) {
    spindles_tools_changed_cbs.push_back(spindlesToolsChangeCb);
}

void MachineInterface::addConnectedCb(std::function<void (MachineInterface*)> connCb) {
    connected_cbs.push_back(connCb);
}

// State management
bool MachineInterface::isHomed(const std::vector<int>& axes) {
    if (axes.empty()) {
        for (bool homed : axes_homed) {
            if (!homed) return false;
        }
        return true;
    } else {
        for (int ax : axes) {
            if (!axes_homed[ax]) return false;
        }
        return true;
    }
}

void MachineInterface::sendGcode(const std::string& gcode, int pollState) {
    if (gcode_queue.size() >= MAX_GCODE_Q_LEN - 2) {
        processGcodeQ();
    }

    gcode_queue.push_back(gcode);
    this->poll_state |= pollState;
    Serial.println(gcode.c_str()); // Debug print (replace with logging)
}

void MachineInterface::processGcodeQ(int max) {
    if (!gcode_queue.empty()) {
        for (const std::string& gcode : gcode_queue) {
            _sendGcode(gcode);
            if (_hasResponse()) {
                // TODO: act on response and possible failure.
                Serial.println(_readResponse().c_str());
            }
        }
        gcode_queue.clear(); // Remove all elements after processing
    }
}

int MachineInterface::nextPollState() {
  int pollState = (polli % 19 == 0) ? PollState::MACHINE_POSITION_EXT : PollState::MACHINE_POSITION;
  if (polli % 3 == 0) {
      pollState |= PollState::JOB_STATUS;
  }
  if (polli % 5 == 0) {
      pollState |= PollState::MESSAGES_AND_DIALOGS;
  }
  if (polli % 7 == 0) {
      pollState |= PollState::PROBES;
  }
  if (polli % 11 == 0) {
      pollState |= PollState::END_STOPS;
  }
  if (polli % 13 == 0) {
      pollState |= PollState::SPINDLE;
  }
  if (polli % 17 == 0) {
      pollState |= PollState::TOOLS;
  }
  if (polli % 9973 == 0) {
      pollState |= PollState::LIST_MACROS | PollState::LIST_FILES;
  }
  return pollState;
}

void MachineInterface::taskLoopIter() {
  // Send any outstanding commands.
  processGcodeQ();

  // Query machine state.
  _updateMachineState(poll_state); // Changed to non-async call
  Serial.println(debugPrint().c_str());
  for (auto& cb : state_changed_cbs) {
    cb(this);
  }

  polli += 1;
  poll_state = nextPollState();
}

void MachineInterface::maybeExecuteContinuousMove() {
  // Check if there are any continuous moves to execute.
  for (size_t i = 0; i < position.size(); ++i) {
      float wcs = wcs_position[i];
      float mps = moving_target_position[i];
      float ps = position[i];
      float tps = target_position[i];

      Serial.printf("mps: %f, wcs: %f, condition: %d\n", mps, wcs, (mps != 0.0 && wcs != 0.0 && fabs(wcs - mps) < 0.001));

      if (mps == 0.0 || (wcs != 0.0 && fabs(ps - mps) < 0.001)) {
          Serial.printf("REACHED %d, ps: %f, wcs: %f, mps: %f\n", i, ps, wcs, mps);
          moving_target_position[i] = 0.0;

          bool hasNewTarget = (tps != 0.0 && ps != 0.0 && fabs(tps - ps) > 0.001);
          if (hasNewTarget && isContinuousMove()) {
              // Execute any "summed up" continuous moves while other move was executing.
              _moveTo(AXES[i], DEFAULT_FEED, tps - wcs, true); // relative = true
          }
      } else {
          Serial.printf("NOT REACHED %d, pos: %f, wcs_pos: %f, moving_target_position: %f\n", i, position[i], wcs_position[i], moving_target_position[i]);
      }
  }
}

void MachineInterface::positionUpdated() {
    maybeExecuteContinuousMove();
    for (auto& cb : pos_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::homeUpdated() {
    for (auto& cb : home_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::wcsUpdated() {
  for (size_t i = 0; i < moving_target_position.size(); ++i) {
    moving_target_position[i] = 0.0;
    target_position[i] = 0.0;
  }

    for (auto& cb : wcs_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::feedUpdated() {
    for (auto& cb : feed_changes_cbs) {
        cb(this);
    }
}

void MachineInterface::sensorsUpdated() {
    for (auto& cb : sensors_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::dialogsUpdated() {
    for (auto& cb : dialogs_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::spindlesToolsUpdated() {
    for (auto& cb : spindles_tools_changed_cbs) {
        cb(this);
    }
}

void MachineInterface::filesUpdated(const std::string& fdir) {
    if (files_changed_cbs.find(fdir) != files_changed_cbs.end()) {
        for (auto& cb : files_changed_cbs[fdir]) {
            cb(this, fdir, files[fdir]);
        }
    }
}

void MachineInterface::connectedUpdated() {
    for (auto& cb : connected_cbs) {
        cb(this);
    }
}

void MachineInterface::updatePosition(const std::vector<float>& values, const std::vector<float>& valuesWcs) {
  position = values;
  wcs_position = valuesWcs;
  positionUpdated();
}

bool MachineInterface::isContinuousMove() {
    return !move_step; // Note the negation
}

void MachineInterface::_moveTo(const std::string& axis, float feed, float value, bool relative) {
    size_t axi = _axisIdx(axis);
    std::string mode = relative ? "G91" : "G90";
    float pos = value;

    if (relative) {
        moving_target_position[axi] = wcs_position[axi] + value;
    } else {
        moving_target_position[axi] = value;
        pos = moving_target_position[axi];
    }

    Serial.printf("SEND: M120\n%s\nG1 %s%.3f F%.3f\nM121\n", mode.c_str(), axis.c_str(), pos, feed);
    sendGcode("M120\n" + mode + "\nG1 " + axis + String(pos, 3).c_str() + " F" + String(feed, 3).c_str() + "\nM121", PollState::MACHINE_POSITION);
}

void MachineInterface::moveContinuous(const std::string& axis, float feed, int direction) {
    Serial.println("> CONTINUOUS");
    unsigned long curr = millis();

    // Send continuous move immediately or keep-alive every ~20ms.
    if (last_continuous_tick == 0 || curr - last_continuous_tick >= 20) {
        _continuousMove(axis, feed, direction);
    }
    last_continuous_tick = curr;
}

void MachineInterface::moveContinuousStop() {
    if (last_continuous_tick != 0) {
        Serial.println("< END CONTINUOUS");
        _continuousStop();
        last_continuous_tick = 0;
    }
}

void MachineInterface::move(const std::string& axis, float feed, float value) {
  size_t axi = _axisIdx(axis);
  Serial.printf("move0 target_position[%d]: %f, value: %f\n", axi, target_position[axi], value);
  _moveTo(axis, feed, value, true);
}

// Always a differential move, value is an offset from current position.
void MachineInterface::__moveWithContinues(const std::string& axis, float feed, float value) {
  size_t axi = _axisIdx(axis);
  Serial.printf("move0 target_position[%d]: %f, value: %f\n", axi, target_position[axi], value);

  bool hasTarget = (target_position[axi] != 0.0);
  bool hasPos = (position[axi] != 0.0);
  bool continuous = isContinuousMove();

  Serial.printf("move continuous: %d, hasTarget: %d, target_position[%d]: %f, value: %f\n", continuous, hasTarget, axi, target_position[axi], value);

  if (!hasTarget) target_position[axi] = wcs_position[axi];
  target_position[axi] += value;

  Serial.printf("move2 continuous: %d, hasTarget: %d, target_position[%d]: %f, value: %f\n", continuous, hasTarget, axi, target_position[axi], value);

  if (!continuous || !hasPos) {
      _moveTo(axis, feed, value, true);
  } else {
      Serial.printf("cont move continuous: %d, hasTarget: %d, target_position[%d]: %f, value: %f\n", continuous, hasTarget, axi, target_position[axi], value);
      maybeExecuteContinuousMove();
  }
}

void MachineInterface::homeAll() {
    sendGcode("G28", PollState::MACHINE_POSITION);
}

void MachineInterface::home(const std::string& axes) {
  size_t axi = _axisIdx(axes);
  target_position[axi] = 0.0;
  sendGcode("G28 " + axes, PollState::MACHINE_POSITION);
}

void MachineInterface::home(const std::vector<std::string>& axes) {
    std::string axesStr;
    for (const std::string& ax : axes) {
      size_t axi = _axisIdx(ax);
      target_position[axi] = 0.0;
        axesStr += ax;
    }
    sendGcode("G28 " + axesStr, PollState::MACHINE_POSITION);
}

std::string MachineInterface::getWcsStr(int wcsOffs) {
    std::string wcsStr = "";
    int wcsi = (wcsOffs == -1) ? this->wcs : wcsOffs;
    if (wcsi <= 5) {
        wcsStr = std::to_string(54 + wcsi);
    } else if (wcsi <= 8) {
        wcsStr = "59." + std::to_string(wcsi - 5);
    }
    return "G" + wcsStr;
}

void MachineInterface::setWcs(int wcs) {
    wcs = wcs % 9;
    std::string wcsStr = getWcsStr(wcs);
    sendGcode(wcsStr, PollState::MACHINE_POSITION);
}

void MachineInterface::setWcsZero(int wcs, const std::vector<std::string>& axes) {
    std::string zer;
    for (const std::string& ax : axes) {
        zer += ax + "0 ";
    }
    // Remove trailing space
    if (!zer.empty()) {
        zer.pop_back();
    }
    sendGcode("G10 L20 P" + std::to_string(wcs) + " " + zer, PollState::MACHINE_POSITION);
}

void MachineInterface::nextWcs() {
    setWcs(wcs + 1);
}

std::string MachineInterface::debugPrint() {
    // Use a JSON library or custom formatting
    std::stringstream ret;
    ret << "{ \"status\": " << String(machine_status) << ", "
      << "\"homed\": [" << String(axes_homed[0]) << ", " << String(axes_homed[1]) << ", " << String(axes_homed[2]) <<  "], ";
    ret << "\"pos\": [" << String(position[0], 3) << ", " << String(position[1], 3) << ", " << String(position[2], 3) << "], ";
    ret << "\"wcs_pos\": [" << String(wcs_position[0], 3) << ", " << String(wcs_position[1], 3) << ", " << String(wcs_position[2], 3) << "], ";
    ret << "\"wcs\": " << String(wcs) << ", ";
    ret << "\"tool\": \"" << tool << "\", ";
    ret << "\"feedm\": " << String(feed_multiplier, 3) << ", ";
    ret << "\"zoffs\": " << String(z_offs, 3) << ", ";
    ret << "\"probes\": [" << String(probes[0], 3) << ", " << String(probes[1], 3) << "], ";
    // ret << "\"end_stops\": " << String(end_stops) << ", ";  // Needs custom serialization
    // ret << "\"spindles\": " << String(spindles) << ", ";  // Needs custom serialization
    // ret << "\"dialogs\": " << String(dialogs) << ", ";  // Needs custom serialization
    ret << "\"gcode_q\": [";
    for (size_t i = 0; i < gcode_queue.size(); ++i) {
        ret << "\"" << gcode_queue[i] << "\"";
        if (i < gcode_queue.size() - 1) ret << ", ";
    }
    ret << "], ";
    ret << "\"poll_state\": " << String(poll_state);
    ret << "}";
    return ret.str();
}

// Helper function to convert axis name to index
size_t MachineInterface::_axisIdx(const std::string& ax) {
    auto it = std::find(AXES.begin(), AXES.end(), ax);
    if (it != AXES.end()) {
        return std::distance(AXES.begin(), it);
    }
    return 0; // Default to 0 if not found (or throw an exception)
}

#if 0

// ============================================================================
// UART Simulation Classes (Conditional Compilation)
// ============================================================================

#if RUN_SIM

#include <sstream>

UART::UART(int pos, long baud, int tx, int rx, int rxbuf) : baudRate(baud) {
    position = {0.0, 0.0, 0.0};
    axesHomed = {false, false, false};
    wcs = 0;
    feedMultiplier = 1.0;
    wcsOffsets = {
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
      {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    };
}

void UART::begin(long baud) {
  baudRate = baud;
}

bool UART::available () {
    return true;
}

void UART::write(const std::string& gcodes) {
    std::stringstream ss(gcodes);
        std::string gcode;
    while (std::getline(ss, gcode, '\n')) {
        if (!gcode.empty()) {
            std::stringstream lineStream(gcode);
            std::string command;
            lineStream >> command;
            std::vector<std::string> args;
            std::string arg;
            while (lineStream >> arg) {
                args.push_back(arg);
            }

            last = command;
            lastArgs = args;
            processGcode(last, lastArgs);
        }
    }
}

bool UART::available() {
    return !last.empty();
}

std::string UART::readLine() {
    return read();
}

std::string UART::read() {
    std::string tempLast = last;
    std::vector<std::string> tempLastArgs = lastArgs;
    last = "";
    lastArgs.clear();
    return processResponse(tempLast, tempLastArgs);
}

void UART::processGcode(const std::string& command, const std::vector<std::string>& args) {
    std::map<char, int> axisIndex = {{'X', 0}, {'Y', 1}, {'Z', 2}};

    if (command == "G28") {
        axesHomed = {true, true, true};
        position = {0.0, 0.0, 0.0};
    } else if (command == "G53") {
        wcs = 0;
    } else if (command == "G54") {
        wcs = 1;
    } else if (command == "G55") {
        wcs = 2;
    } else if (command == "G56") {
        wcs = 3;
    } else if (command == "G57") {
        wcs = 4;
    } else if (command == "G58") {
        wcs = 5;
    } else if (command == "G59") {
        wcs = 6;
    } else if (command == "G59.1") {
        wcs = 7;
    } else if (command == "G59.2") {
        wcs = 8;
    } else if (command == "G59.3") {
        wcs = 9;
    } else if (command == "G10") {
        int wcsIndex = 0;
        int axis = 0;
        float value = 0.0;
        for (const std::string& arg : args) {
            char cmd = toupper(arg[0]);
            if (cmd == 'P') {
                wcsIndex = std::stoi(arg.substr(1));
            } else if (cmd == 'L') {
                if (arg != "L20") {
                    throw std::runtime_error("Only L20 supported");
                }
            } else {
                axis = axisIndex[cmd];
                value = std::stof(arg.substr(1));
            }
        }
        wcsOffsets[axis][wcsIndex - 1] = value - position[axis];
    } else if (command == "G1" || command == "G0") {
        for (const std::string& arg : args) {
            char axis = toupper(arg[0]);
            if (axisIndex.count(axis)) {
                float value = std::stof(arg.substr(1));
                position[axisIndex[axis]] += value;
            }
        }
    }
}

std::string UART::processResponse(const std::string& command, const std::vector<std::string>& args) {
    if (command == "M409") {
        std::string key = args[0].substr(2);
        key = key.substr(0, key.length() - 1);
        if (key == "move.axes" || key == "move.axes[]") {
            float x = position[0];
            bool xh = axesHomed[0];
            float xu = x + wcsOffsets[0][wcs];
            float y = position[1];
            bool yh = axesHomed[1];
            float yu = y + wcsOffsets[1][wcs];
            float z = position[2];
            bool zh = axesHomed[2];
            float zu = z + wcsOffsets[2][wcs];
            std::string xwcs_repr = "[";
            for(int i = 0; i < wcsOffsets[0].size(); i++) {
              xwcs_repr += String(wcsOffsets[0][i], 3).c_str();
              if(i < wcsOffsets[0].size() - 1) xwcs_repr += ", ";
            }
            xwcs_repr += "]";
            std::string ywcs_repr = "[";
            for(int i = 0; i < wcsOffsets[1].size(); i++) {
              ywcs_repr += String(wcsOffsets[1][i], 3).c_str();
              if(i < wcsOffsets[1].size() - 1) ywcs_repr += ", ";
            }
            ywcs_repr += "]";
            std::string zwcs_repr = "[";
            for(int i = 0; i < wcsOffsets[2].size(); i++) {
              zwcs_repr += String(wcsOffsets[2][i], 3).c_str();
              if(i < wcsOffsets[2].size() - 1) zwcs_repr += ", ";
            }
            zwcs_repr += "]";

            return "{\"key\":\"move.axes\",\"flags\":\"\",\"result\":[{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":" + (xh ? "true" : "false") + ",\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":" + String(x, 3).c_str() + ",\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":" + String(xu, 3).c_str() + ",\"visible\":true,\"workplaceOffsets\":" + xwcs_repr + "},{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":" + (yh ? "true" : "false") + ",\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":" + String(y, 3).c_str() + ",\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":" + String(yu, 3).c_str() + ",\"visible\":true,\"workplaceOffsets\":" + ywcs_repr + "},{\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1,\"drivers\":[\"0.3\"],\"homed\":" + (zh ? "true" : "false") + ",\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":" + String(z, 3).c_str() + ",\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":" + String(zu, 3).c_str() + ",\"visible\":true,\"workplaceOffsets\":" + zwcs_repr + "}],\"next\":0}\n";
        } else if (key == "network") {
            return "{\"key\":\"network\",\"flags\":\"\",\"result\":{\"corsSite\":\"\",\"hostname\":\"mininc\",\"interfaces\":[{\"actualIP\":\"0.0.0.0\",\"firmwareVersion\":\"(unknown)\",\"gateway\":\"0.0.0.0\",\"state\":\"disabled\",\"subnet\":\"255.255.255.0\",\"type\":\"wifi\"}],\"name\":\"MiniNC\"}}\n";
        } else if (key == "move.currentMove") {
            int speed = random(0, 5000);
            return "{\"key\":\"move.currentMove\",\"flags\":\"\",\"result\":{\"acceleration\":0,\"deceleration\":0,\"extrusionRate\":0,\"requestedSpeed\":" + String(speed).c_str() + ",\"topSpeed\":" + String(random(speed - 1000, speed)).c_str() + "}}\n";
        } else if (key == "move.speedFactor") {
            return "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":" + String(feedMultiplier, 3).c_str() + "}\n";
        } else if (key == "job") {
            return "{\"key\":\"job\",\"flags\":\"d3\",\"result\":{\"file\":{\"filament\":[],\"height\":0,\"layerHeight\":0,\"numLayers\":0,\"size\":0,\"thumbnails\":[]},\"filePosition\":0,\"lastDuration\":0,\"lastWarmUpDuration\":0,\"timesLeft\":{}}}\n";
        } else if (key == "global") {
            return "{\"key\":\"global\",\"flags\":\"\",\"result\":{\"varsLoaded\":true,\"parkZ\":2}}\n";
        } else if (key == "state.messageBox") {
            return "{\"key\":\"state.messageBox\",\"flags\":\"\",\"result\":null}\n";
        } else if (key == "sensors.endstops[]") {
            return "{\"key\":\"sensors.endstops[]\",\"flags\":\"\",\"result\":[null,null,null],\"next\":0}\n";
        } else if (key == "move.workplaceNumber") {
            return "{\"key\":\"move.workplaceNumber\",\"flags\":\"\",\"result\":" + String(wcs).c_str() + "}\n";
        } else if (key == "sensors.probes[].value[]") {
            return "{\"key\":\"sensors.probes[].values[]\",\"flags\":\"\",\"result\":[0,10],\"next\":0}\n";
        } else if (key == "") {
            return "{\"key\":\"\",\"result\":null}\n";
        } else {
            throw std::runtime_error("Unknown arg to M409: " + key);
        }
    } else if (command == "M20") {
        for (const std::string& arg : args) {
            char cmd = toupper(arg[0]);
            if (cmd == 'P') {
                std::string s = arg.substr(1);
                s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
                if (s.rfind("/macro", 0) == 0) {
                    return "{\"dir\":\"/macros/\",\"first\":0,\"files\":[\"probe_work.g\",\"free_z.g\",\"all_zero.g\",\"z_zero.g\",\"zero_workspace.g\",\"work_align_xy10mm.gcode\",\"move_free.g\",\"touch_probe_work.g\"],\"next\":0,\"err\":0}\n";
                } else if (s.rfind("/gcode", 0) == 0) {
                    return "{\"dir\":\"/gcodes/\",\"first\":0,\"files\":[\"Updown.gcode\",\"Updown1.gcode\",\"updown 6.1.gcode\",\"MiniNC Z Plate.gcode\",\"updown 6.1 5mm-adaptive.gcode\",\"updown 6.1 - adaptive 5mm, pocket 0.5mm-0.75mm, slot 0.5mm.gcode\",\"MiniNC Z Plate Contout Only.gcode\"],\"next\":0,\"err\":0}\n";
                }
                return "{\"dir\":\"" + s + "\",\"err\":2}";
            }
        }
    } else if (!command.empty()) {
        Serial.print("UARTSim READ: UNKNOWN LAST GCODE ");
        Serial.print(command.c_str());
        Serial.print(" ");
        for (const std::string& arg : args) {
            Serial.print(arg.c_str());
            Serial.print(" ");
        }
        Serial.println();
        return "ok\n";
    } else {
        return "";
    }
    return "";
}

#else

UART::UART(int portNum, long baud, int txPin, int rxPin, int rxBufSize) : HardwareSerial(portNum) {
    begin(baud, SERIAL_8N1, txPin, rxPin);  // Specify txPin and rxPin
    // Use the following to set RX buffer size. Must be a power of 2.
    setRxBufferSize(rxBufSize);
}

std::string UART::readLine() {
    String line = readStringUntil('\n');
    return line.c_str();
}

// ============================================================================
// MachineRRF Class (Subclass of MachineInterface)
// ============================================================================


const std::map<char, int> MachineRRF::AXIS_NAMES = {{'X', 0}, {'Y', 1}, {'Z', 2}, {'U', 3}, {'V', 4}, {'A', 5}, {'B', 6}};
const std::map<char, int> MachineRRF::RRF_TO_STATUS = {
    {'C', MachineStatus::INITIALIZING},
    {'F', MachineStatus::FLASHING_FIRMWARE},
    {'H', MachineStatus::EMERGENCY_HALTED},
    {'O', MachineStatus::OFF},
    {'D', MachineStatus::PAUSED_DEC},
    {'R', MachineStatus::PAUSED_RESUME},
    {'S', MachineStatus::PAUSED},
    {'M', MachineStatus::SIMULATING},
    {'P', MachineStatus::RUNNING},
    {'T', MachineStatus::TOOL_CHANGING},
    {'B', MachineStatus::BUSY}
};

MachineRRF::MachineRRF(int sleepMs) : MachineInterface(sleepMs), uart(2, 115200, 43, 44, 1024*16) {
    connected = false;
    connectedUpdated();
    input_sel = "";
}

// Overridden methods from MachineInterface
void MachineRRF::_sendGcode(const std::string& gcode) {
    std::stringstream ss(gcode);
    std::string line;
    while (std::getline(ss, line, '\n')) {
        uart.write(line.c_str());
        uart.write('\n');
    }
}

bool MachineRRF::_hasResponse() {
    return uart.available();
}

std::string MachineRRF::_readResponse() {
    return uart.readLine();
}

void MachineRRF::_updateMachineState(int pollState) {
    if (PollState::hasState(pollState, PollState::MACHINE_POSITION)) {
        _updateFeedMultiplier();
        _updateWcs();
        _procMachineState("M409 K\"move.axes[]\" F\"d5,f\"");
        _procMachineState("M409 K\"" + input_sel + "\"");
    }
    if (PollState::hasState(pollState, PollState::MACHINE_POSITION_EXT)) {
        _procMachineState("M409 K\"move.axes[]\" F\"d5\"");
    }
    if (PollState::hasState(pollState, PollState::NETWORK)) {
        _updateNetworkInfo();
    }
    if (PollState::hasState(pollState, PollState::JOB_STATUS)) {
        _updateCurrentJob();
    }
    if (PollState::hasState(pollState, PollState::MESSAGES_AND_DIALOGS)) {
        _updateMessageBox();
    }
    if (PollState::hasState(pollState, PollState::END_STOPS)) {
        _updateEndstops();
    }
    if (PollState::hasState(pollState, PollState::PROBES)) {
        _updateProbeVals();
    }
    if (PollState::hasState(pollState, PollState::SPINDLE)) {
        _updateSpindles();
    }
    if (PollState::hasState(pollState, PollState::TOOLS)) {
        _updateTools();
    }
}

void MachineRRF::listFiles(const std::string& path) {
    sendGcode("M20 S2 P\"/" + path + "/\"", 0);
    // sendGcode("M20 S2 P\"/" + path + "/\"", PollState::LIST_FILES);

}

void MachineRRF::runMacro(const std::string& macroName) {
    _sendGcode("M98 P\"" + macroName + "\"");
}

void MachineRRF::startJob(const std::string& jobName) {
    _sendGcode("M23 " + jobName);
    _sendGcode("M24");
}

bool MachineRRF::isConnected() {
    return connected;
}

// Helper methods
void MachineRRF::_procMachineState(const std::string& cmd) {
    _sendGcode(cmd);
    std::string res = "";
    if (uart.available() > 0) {
        res = uart.readLine();
        parseJsonResponse(res);
        if (!connected) {
            positionUpdated();
            wcsUpdated();
            homeUpdated();
        }
        if (!connected) {
            connected = true;
            connectedUpdated();
        }
    } else {
        Serial.printf("Timeout or Error: no data available\n");
        if (connected) {
            connected = false;
            connectedUpdated();
        }
    }
}

void MachineRRF::_updateNetworkInfo() {
    _procMachineState("M409 K\"network\"");
}

void MachineRRF::_updateFeedMultiplier() {
    _procMachineState("M409 K\"move.speedFactor\"");
}

void MachineRRF::_updateCurrentJob() {
    _procMachineState("M409 K\"job\" F\"d3\"");
}

void MachineRRF::_updateMessageBox() {
    _procMachineState("M409 K\"state.messageBox\"");
}

void MachineRRF::_updateEndstops() {
    _procMachineState("M409 K\"sensors.endstops[]\"");
}

void MachineRRF::_updateProbeVals() {
    _procMachineState("M409 K\"sensors.probes[].value[]\"");
}

void MachineRRF::_updateWcs() {
    _procMachineState("M409 K\"move.workplaceNumber\"");
}

void MachineRRF::_updateSpindles() {
    spindlesToolsUpdated();
    // TODO: Implement spindle update logic
}

void MachineRRF::_updateTools() {
    spindlesToolsUpdated();
    // TODO: Implement tool update logic
}

void MachineRRF::parseMoveAxesBrief(const JsonArray& res) {
    bool updated = false;
    for (size_t i = 0; i < res.size(); ++i) {
        JsonObject axis = res[i];
        float machinePos = axis["machinePosition"];
        float wcsPos = axis["userPosition"];

        if (position[i] != machinePos || wcs_position[i] != wcsPos) {
            updated = true;
        }
        position[i] = machinePos;
        wcs_position[i] = wcsPos;
    }
    if (updated) positionUpdated();
}

void MachineRRF::parseMoveAxes(const JsonArray& res) {
    JsonObject first = res[0];
    const char *letter = res[0]["letter"];
    if (res) {
        parseMoveAxesExt(res);
    } else {
        parseMoveAxesBrief(res);
    }
}

void MachineRRF::parseMoveAxesExt(const JsonArray& res) {
  bool updated = false;
  bool home_updated = false;

  for (size_t i = 0; i < res.size(); ++i) {
      JsonObject axis = res[i];
      std::string name = axis["letter"].as<std::string>();
      int index = AXIS_NAMES.at(name[0]);
      bool homed = axis["homed"];
      float machinePos = axis["machinePosition"];
      float wcsPos = axis["userPosition"];

      if(axes_homed[index] != homed) home_updated = true;
      axes_homed[index] = homed;

      if (position[index] != machinePos || wcs_position[index] != wcsPos) {
          updated = true;
      }
      position[index] = machinePos;
      wcs_position[index] = wcsPos;
  }
  if (updated) positionUpdated();
  if (home_updated) homeUpdated();
}

void MachineRRF::parseM20Response(const JsonObject& jsonResp) {
    std::string jdir = jsonResp["dir"].as<std::string>();
    std::string fdir = jdir.replace(jdir.begin(), jdir.begin() + 1, "");
    JsonArray filesArr = jsonResp["files"];
    std::vector<std::string> filesVec;
    for (JsonVariant v : filesArr) {
        filesVec.push_back(v.as<std::string>());
    }
    files[fdir] = filesVec;
    filesUpdated(fdir);
}

void MachineRRF::parseM409Response(const JsonObject& j) {
    std::string key = j["key"].as<std::string>();
    JsonVariant res = j["result"];

    //try {
    {
        if (key == "move.axes" || key == "move.axes[]") {
            parseMoveAxes(res.as<JsonArray>());
        } else if (key == "global") {
            //parseGlobals(res.as<JsonObject>());  // TODO: Implement parseGlobals
        } else if (key == "job") {
            JsonObject jobData = res.as<JsonObject>();
            job["duration"] = String(jobData["duration"].as<float>()).c_str();
            JsonObject fileData = jobData["file"];
            job["file"] = fileData["fileName"].as<std::string>();
            job["time_total"] = String(fileData["printTime"].as<float>()).c_str();
            job["time_sim"] = String(fileData["simulatedTime"].as<float>()).c_str();
            JsonObject timesLeft = jobData["timesLeft"];
            job["time_remain"] = String(timesLeft["file"].as<float>()).c_str();
        } else if (key == "move.workplaceNumber") {
            int workplaceNumber = res.as<int>();
            if (workplaceNumber != wcs) {
                wcs = workplaceNumber;
                wcsUpdated();
            }
        } else if (key == "move.current_move") {
            JsonObject currentMove = res.as<JsonObject>();
            feed = currentMove["topSpeed"].as<int>();
            feed_req = currentMove["requestedSpeed"].as<int>();
        } else if (key == "move.speedFactor") {
            float speedFactor = res.as<float>();
            if (feed_multiplier != speedFactor) {
                feed_multiplier = speedFactor;
                feedUpdated();
            }
        } else if (key == "network") {
            network.clear();
            JsonObject networkData = res.as<JsonObject>();
            std::string host = networkData["hostname"].as<std::string>();
            JsonArray interfaces = networkData["interfaces"].as<JsonArray>();
            for (JsonVariant iface : interfaces) {
                JsonObject ifaceData = iface.as<JsonObject>();
                std::string ip = ifaceData["actualIP"].as<std::string>();
                // network.push_back({"hostname", host});
                // network.push_back({{"ip", ip}});
            }
        } else if (key == "state.messageBox") {
            // TODO: add actual message box parsing
        } else if (key == "sensors.probes[].value[]") {
            JsonArray probeValues = res.as<JsonArray>();
            for(int i = 0; i < probeValues.size(); i++) {
              probes[i] = probeValues[i].as<float>();
            }
            sensorsUpdated();
        } else if (key == "state.thisInput") {
            int inputIndex = res.as<int>();
            std::stringstream ss;
            ss << "inputs[" << inputIndex << "].axesRelative";
            input_sel = ss.str();
        } else if (key == input_sel) {
            // TODO: add actual input parsing
            // if (move_relative != res) {
            //   move_relative = res;
            //   positionUpdated();
            // }
        }
    } 
    //catch (const std::exception& e) {
    //    Serial.printf("Failed to read json, unknown key: %s, error: %s\n", key.c_str(), e.what());
    //}
}

void MachineRRF::parseJsonResponse(const std::string& jsonResp) {
  // TODO: seq-based major updates.
  JsonDocument doc; // Adjust size as needed
  DeserializationError error = deserializeJson(doc, jsonResp);

  if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
  }

  if (!doc["dir"].isNull()) {
      parseM20Response(doc.as<JsonObject>());
      return;
  }

  if (!doc["key"].isNull() && !doc["result"].isNull()) {
      parseM409Response(doc.as<JsonObject>());
      return;
  }
  Serial.println("Unrecognized json");
  Serial.println(jsonResp.c_str());
}

void MachineRRF::_continuousStop() {
    _sendGcode("M98 P\"pendant-continuous-stop.g\"");
}

void MachineRRF::_continuousMove(const std::string& axis, float feed, int direction) {
    _sendGcode("M98 P\"pendant-continuous-run.g\" A\"" + axis + "\" F" + String(feed).c_str() + " D" + String(direction).c_str());
}

#endif

#endif

// ============================================================================
// Global Variables and Functions
// ============================================================================

MachineInterface* machine; // Global machine interface

void updateState(MachineInterface* m) {
  // This is where you would update your UI with the new state
  // For example:
  // lv_label_set_text(ui_StatusLabel, String(m->machine_status).c_str());
  Serial.println("status");
}