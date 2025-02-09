#ifndef MACHINE_POSITION_WCS_H
#define MACHINE_POSITION_WCS_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <string>
#include <map>

#include "machine/machine_interface.hpp"
#include "ui/interface.hpp"

class MachinePositionWCS {
public:
    static const int FONT_WIDTH = 11;
    static const int FONT_HEIGHT = 22;
    static const std::vector<std::string> DEFAULT_COORD_SYSTEMS;

    MachinePositionWCS(lv_obj_t* parent, const std::vector<std::string>& coords,
                       Interface* interface, int digits = 5,
                       const std::vector<std::string>& coord_systems = DEFAULT_COORD_SYSTEMS,
                       int coord_sys_width = 40);

    void coordsUndefined();
    void setCoord(int ax, float v, const std::string& coord_system = "");
    void setCoord(const std::string& ax, float v, const std::string& coord_system = "");

    lv_obj_t *self;
private:
    Interface* interface;
    lv_obj_t* parent;
    std::vector<std::string> coords;
    int digits;
    std::string fmt_str;
    std::vector<std::string> coord_systems;

    std::map<std::string, std::string> axis_label_ids;
    std::vector<lv_obj_t*> axis_labels;
    std::vector<lv_obj_t*> coord_sys_labels;
    std::map<std::string, std::vector<lv_obj_t*>> coord_val_labels;
    std::vector<std::string> coord_val_labels_by_id;
    std::map<std::string, std::vector<float>> coord_vals;

    void _label_home_clicked(lv_event_t* e);
    void _pos_updated(MachineInterface* mach);
    void _home_updated(MachineInterface* mach);
    void _wcs_updated(MachineInterface* mach);
};


class MachinePosition {
public:
    static const int FONT_WIDTH = 7;

    MachinePosition(lv_obj_t* parent, const std::vector<std::string>& coords,
                       Interface* interface, int digits = 5);

    void coordsUndefined();
    void setCoord_(int c, float v);
    void setCoord_(const std::string& c, float v);

    lv_obj_t* container;
private:
    Interface* interface;
    lv_obj_t* parent;
    lv_obj_t *self;
    std::vector<std::string> coords;
    int digits;
    std::string fmt_str;

    std::vector<float> coord_vals;

    std::vector<lv_obj_t*> coord_labels;
};


#endif