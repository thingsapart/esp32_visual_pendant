#include "machine_rrf.hpp"

// ============================================================================
// Constants
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

// ============================================================================
// Simulated UART Implementation (Conditional)
// ============================================================================

#if RUN_SIM
#include <sstream>
#include <map>
#include <random>

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

bool UART::available () {
    return true;
}

void UART::write(const std::string& gcodes) {
    std::map<char, int> axisIndex = {{'X', 0}, {'Y', 1}, {'Z', 2}};

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
            std::random_device rd;
            std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
            std::uniform_int_distribution<> distrib(0, 5000);
            int speed = distrib(gen);

            std::uniform_int_distribution<> distrib2(speed - 1000, speed);
            int top_speed = distrib2(gen);
            return "{\"key\":\"move.currentMove\",\"flags\":\"\",\"result\":{\"acceleration\":0,\"deceleration\":0,\"extrusionRate\":0,\"requestedSpeed\":" + String(speed).c_str() + ",\"topSpeed\":" + String(top_speed).c_str() + "}}\n";
        } else if (key == "move.speedFactor") {
            return "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":" + String(feedMultiplier, 3).c_str() + "}\n";
        } else if (key == "job") {
            return "{\"key\":\"job\",\"flags\":\"d3\",\"result\":{\"file\":{\"filament\":[],\"height\":0,\"layerHeight\":0,\"numLayers\":0,\"size\":0,\"thumbnails\":[]},"
                   "\"filePosition\":0,\"lastDuration\":0,\"lastWarmUpDuration\":0,\"timesLeft\":{}}}\n";
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

#endif

// ============================================================================
// MachineRRF Class Implementation
// ============================================================================

MachineRRF::MachineRRF(int sleepMs) : MachineInterface(sleepMs), uart(2, 115200, 43, 44, 1024*16) {
    connected = false;
    connectedUpdated();
    input_sel = "";
    input_idx = 0;
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
            input_idx = res.as<int>();
            std::stringstream ss;
            ss << "inputs[" << input_idx << "].axesRelative";
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

void MachineRRF::parseM408(const std::string& json_resp) {
  // TODO: implement stub
}

void MachineRRF::_continuousStop() {
    _sendGcode("M98 P\"pendant-continuous-stop.g\"");
}

void MachineRRF::_continuousMove(const std::string& axis, float feed, int direction) {
    _sendGcode("M98 P\"pendant-continuous-run.g\" A\"" + axis + "\" F" + String(feed).c_str() + " D" + String(direction).c_str());
}
