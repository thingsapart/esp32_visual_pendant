#ifndef TAB_JOG_HPP
#define TAB_JOG_HPP

#include <Arduino.h>
#include <lvgl.h>
#include <string>

// Forward declarations
class Interface;  // Assuming Interface class is defined elsewhere
class MachineInterface; // Assuming MachineInterface is defined elsewhere
class JogDial;

class TabJog {
public:
    TabJog(lv_obj_t* tabv, Interface* interface, lv_obj_t* tab);
    ~TabJog();

private:
    lv_obj_t* tabv;
    Interface* interface;
    lv_obj_t* tab;
    JogDial* jog_dial;
};

#endif // TAB_JOG_HPP