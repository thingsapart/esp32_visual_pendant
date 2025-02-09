#include "ui/tab_jog.hpp"
#include "ui/jog_dial.hpp"  // Include JogDial's header
#include <Arduino.h>

// Forward declarations
class Interface;  // Assuming Interface class is defined elsewhere
class MachineInterface; // Assuming MachineInterface is defined elsewhere
class JogDial;

TabJog::TabJog(lv_obj_t* tabv, Interface* interface, lv_obj_t* tab) :
        tabv(tabv),
        interface(interface),
        tab(tab) {
    this->tabv = tabv;
    this->interface = interface;
    this->tab = tab;
    this->jog_dial = new JogDial(this->tab, interface);  // Create the JogDial instance
}

TabJog::~TabJog() {
    if (jog_dial != nullptr) {
        delete jog_dial;
    }
}