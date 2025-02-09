#include "interface.hpp"

#include "ui/tab_jog.hpp"     // These headers should define the C++ Tab classes
//#include "ui/tab_probe.h"
//#include "ui/tab_machine.h"

// Forward declarations for the UI classes
class TabProbe;
class TabMachine;
class MachineInterface;

Interface::Interface(MachineInterface* machine) : machine(machine) {
    scr = lv_screen_active();
    this->machine = machine;
    fs_init();
    init_fonts();

    wheel_tick = nullptr;
    wheel_tick_target = nullptr;

    init_main_tabs();

    lv_screen_load(scr);
}

Interface::~Interface() {
    if (tab_jog != nullptr) delete tab_jog;
    // if (tab_probe != nullptr) delete tab_probe;
    // if (tab_machine != nullptr) delete tab_machine;

    // if(font_lcd != nullptr) lv_font_delete(font_lcd);
    // if(font_lcd_18 != nullptr) lv_font_delete(font_lcd_18);
    // if(font_lcd_24 != nullptr) lv_font_delete(font_lcd_24);
}

bool Interface::processWheelTick(int32_t diff) {
    if (wheel_tick == nullptr) {
        return false;
    }

    wheel_tick(diff);
    return true;
}

void Interface::registerStateChangeCb(std::function<void (MachineInterface *)> cb) {
    machine_change_callbacks.push_back(cb);
}

void Interface::updateMachineState(MachineInterface* machine) {
    // TODO: Update global displays.
    for (auto& cb : machine_change_callbacks) {
        cb(machine);
    }
}

void Interface::init_fonts() {
    //try {
    {
        // Construct the paths relative to the current directory
        std::string script_path = "."; // Default to the project root

        font_lcd = lv_binfont_create("S:../font/lcd_7_segment.bin");
        font_lcd_18 = lv_binfont_create("S:../font/lcd_7_segment_18.bin");
        font_lcd_24 = lv_binfont_create("S:../font/lcd_7_segment_24.bin");

        if (font_lcd == nullptr) {
            Serial.println("Failed to load font: lcd_7_segment.bin");
        }
        if (font_lcd_18 == nullptr) {
            Serial.println("Failed to load font: lcd_7_segment_18.bin");
        }
        if (font_lcd_24 == nullptr) {
            Serial.println("Failed to load font: lcd_7_segment_24.bin");
        }
    } /*catch (const std::exception& e) {
        Serial.printf("Failed to load font: %s\n", e.what());
        font_lcd = nullptr; // Ensure it's null if loading fails
        font_lcd_18 = nullptr;
        font_lcd_24 = nullptr;
    }*/
}

void Interface::fs_init() {
    lv_fs_arduino_esp_littlefs_init();
}

void Interface::init_main_tabs() {
    main_tabs = lv_tabview_create(scr);
    lv_obj_t* tabv = main_tabs;
    lv_tabview_set_tab_bar_size(tabv, TAB_HEIGHT);

    lv_obj_remove_flag(lv_tabview_get_content(tabv), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* tab_jog = lv_tabview_add_tab(tabv, "Jog");
    lv_obj_t* tab_probe = lv_tabview_add_tab(tabv, "Probe");
    lv_obj_t* tab_machine = lv_tabview_add_tab(tabv, "Machine");
    tab_job_gcode = lv_tabview_add_tab(tabv, "Status");
    tab_tool = lv_tabview_add_tab(tabv, "Tools");
    tab_cam = lv_tabview_add_tab(tabv, "CAM");

    this->tab_jog = new TabJog(tabv, this, tab_jog);
    //this->tab_probe = new TabProbe(tabv, this, tab_probe);
    //this->tab_machine = new TabMachine(tabv, this, tab_machine);
}