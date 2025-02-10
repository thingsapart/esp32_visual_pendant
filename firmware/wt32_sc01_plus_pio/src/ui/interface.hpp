#ifndef INTERFACE_HPP
#define INTERFACE_HPP

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <string>
#include <functional>

#include "machine/machine_interface.hpp"
#include "ui/tab_jog.hpp"
#include "ui/debug.hpp"

// Forward declarations for the UI classes
class TabProbe;
class TabMachine;

class Interface {
public:
    static const int TAB_HEIGHT = 30;
    static const int TAB_WIDTH = 70;

    MachineInterface* machine;
    lv_font_t* font_lcd = nullptr;
    lv_font_t* font_lcd_18 = nullptr;
    lv_font_t* font_lcd_24 = nullptr;

    Interface(MachineInterface* machine);
    ~Interface();

    bool processWheelTick(int32_t diff);
    void registerStateChangeCb(std::function<void (MachineInterface *)> cb);
    void updateMachineState(MachineInterface* machine);

private:
    void init_fonts();
    void fs_init();
    void init_main_tabs();

private:
    lv_obj_t* scr;
    lv_fs_drv_t fs_drv;

    std::function<void(int32_t)> wheel_tick;
    lv_obj_t* wheel_tick_target;

    lv_obj_t* main_tabs;
    TabJog* tab_jog;
    // TabProbe* tab_probe;
    // TabMachine* tab_machine;
    lv_obj_t* tab_job_gcode;
    lv_obj_t* tab_tool;
    lv_obj_t* tab_cam;

    std::vector<std::function<void (MachineInterface *)>> machine_change_callbacks;
};

#endif // INTERFACE_HPP