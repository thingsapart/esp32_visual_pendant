#ifndef JOG_DIAL_HPP
#define JOG_DIAL_HPP

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <string>
#include <map>

// Forward declarations
class Interface;  // Assuming Interface class is defined elsewhere
class MachineInterface; // Assuming MachineInterface is defined elsewhere
class MachinePositionWCS;
// class MachinePosition;
// class style;

class JogDial {
public:
    static const std::vector<std::tuple<std::string, float>> FEEDS;
    static const std::vector<std::string> AXES_OPTIONS;
    static const std::vector<std::string> AXES;
    static const std::map<std::string, int> AXIS_IDS;

    JogDial(lv_obj_t* parent, Interface* interface);
    ~JogDial();

    std::string currentAxis();
    void setAxis(const std::string& ax);
    void setAxisVis(const std::string& ax);
    std::string nextAxis();
    void addAxisChangeDb(void (*cb)(const std::string&));
    void connectionList();
    void updatePosLabels(const std::vector<float>& vals);
    bool axisSelected();
    void inc();
    void dec();
    void setValue(int v);

private:
    void _machineStateUpdated(MachineInterface* machine);
    void _axisClicked(lv_event_t* evt);
    void _feedClicked(lv_event_t* evt);
    void _feedChanged(lv_event_t* evt);
    void _jogDialValueChangedEventCb(lv_event_t* evt);
    void initPosLabels_();

private:
    int prev;
    lv_obj_t* parent;
    Interface* interface;
    float feed;
    std::string axis;
    int axis_id;
    int last_rotary_pos;
    std::vector<void (*)(const std::string&)> axis_change_cb;

    lv_obj_t* axis_btns;
    lv_obj_t* arc;
    lv_obj_t* feed_label;
    lv_obj_t* feed_slider;

    MachinePositionWCS* position;
    // MachinePosition* position;
};

#endif // JOG_DIAL_HPP