#include "machine_position.hpp"
#include <stdio.h> // For sprintf
#include <lvgl.h>
#include "ui/interface.hpp"
#include "machine/machine_interface.hpp"

#include "ui/lv_style.hpp"

const std::vector<std::string> MachinePositionWCS::DEFAULT_COORD_SYSTEMS = {"Mach", "WCS", "Move"};

// Implement the color function here, or in a common utility file
int color(const char* name) {
    if (strcmp(name, "BLUE") == 0) return st_color(lv_color_hex(0x0000FF));
    if (strcmp(name, "RED") == 0) return st_color(lv_color_hex(0xFF0000));
    if (strcmp(name, "LIME") == 0) return st_color(lv_color_hex(0x00FF00));
    // Add more colors as needed
    return st_color(lv_color_hex(0x808080)); // Default to gray
}

MachinePositionWCS::MachinePositionWCS(lv_obj_t* parent, const std::vector<std::string>& coords,
                                       Interface* interface, int digits,
                                       const std::vector<std::string>& coord_systems,
                                       int coord_sys_width) {
    this->self = lv_obj_create(parent);
    this->interface = interface;
    this->parent = parent;
    this->coords = coords;
    this->digits = digits;
    char fmt[20];
    sprintf(fmt, "%%%d.2f", digits - 2);
    this->fmt_str = fmt;
    this->coord_systems = coord_systems;

    lv_obj_t* container = self;
    style_container_blank(self);
    lv_obj_set_style_pad_column(container, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(container, 2, LV_STATE_DEFAULT);
    lv_obj_set_layout(container, LV_LAYOUT_GRID);
    non_scrollable(container);
    no_margin_pad_border(container);

    lv_font_t* font = this->interface->font_lcd_24; // Assuming this is how you access the font

    int lblc_width = (digits + 1) * FONT_WIDTH;
    int lencs = coord_systems.size();
    int lenc = coords.size();

    static lv_coord_t col_dsc[10]; // Adjust size as needed for your grid
    col_dsc[0] = coord_sys_width;

    for (int i = 1; i <= lencs; ++i) {
        col_dsc[i] = lblc_width;
    }
    col_dsc[lencs + 1] = LV_GRID_TEMPLATE_LAST;

    static lv_coord_t row_dsc[10]; // Adjust size as needed for your grid
    for (int i = 0; i <= lenc; ++i) {
        row_dsc[i] = FONT_HEIGHT + 2;
    }
    row_dsc[lenc + 1] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_style_grid_column_dsc_array(container, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(container, row_dsc, 0);
    lv_obj_set_size(container, coord_sys_width + lencs * 2 + lblc_width * lencs + 2, LV_SIZE_CONTENT);


    // Create Axis labels.
    for (size_t i = 0; i < coords.size(); ++i) {
        lv_obj_t* lbl = lv_label_create(container);
        std::string text = LV_SYMBOL_HOME " " + coords[i];
        lv_label_set_text(lbl, text.c_str());
        style(lbl, {{"bg_opa", 100}, {"margin", 1}, {"padding", 0}});
        lv_obj_set_grid_cell(lbl, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, i + 1, 1);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(lbl, [](lv_event_t* e) {
            MachinePositionWCS *self = static_cast<MachinePositionWCS*>(lv_event_get_user_data(e));
            self->_label_home_clicked(e);
        }, LV_EVENT_CLICKED, this);

        lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        axis_label_ids[text] = coords[i];
        axis_labels.push_back(lbl);
        lv_color_t colr = t_color(interface->machine->axes_homed[i] ? color("BLUE") : color("RED"));
        lv_obj_set_style_bg_color(lbl, colr, LV_STATE_DEFAULT);
    }


    // Coordinate systems.
    for (size_t i = 0; i < coord_systems.size(); ++i) {
        lv_obj_t* lbl = lv_label_create(container);
        lv_label_set_text(lbl, coord_systems[i].c_str());
        style(lbl, {{"bg_color", color("BLUE")}, {"bg_opa", 100}, {"margin", 1}, {"padding", 0}});
        lv_obj_set_grid_cell(lbl, LV_GRID_ALIGN_STRETCH, i + 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_center(lbl);
        coord_sys_labels.push_back(lbl);

        if (i == 1) {
            lv_obj_add_event_cb(lbl, [](lv_event_t* e) {
                MachinePositionWCS* self = (MachinePositionWCS*) lv_event_get_user_data(e);
                self->interface->machine->nextWcs();
            }, LV_EVENT_CLICKED, this);
            lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    // Coordinate Values
    for (size_t i = 0; i < coord_systems.size(); ++i) {
        std::vector<lv_obj_t*> c_labels;
        coord_val_labels[coord_systems[i]] = c_labels;
        coord_val_labels_by_id.push_back(coord_systems[i]);
        coord_vals[coord_systems[i]] = std::vector<float>(coords.size(), NAN); // Initialize with NAN


        for (size_t j = 0; j < coords.size(); ++j) {
            lv_obj_t* lblc = lv_label_create(container);
            std::string q_marks(digits - 2, '?');
            lv_label_set_text(lblc, (q_marks + ".??").c_str());

            style(lblc, {{"bg_color", color("LIME")}, {"bg_opa", 60}, {"margin", 0}, {"padding", 2}});


            if (font != nullptr) {
                lv_obj_set_style_text_font(lblc, font, LV_STATE_DEFAULT);
            }
            lv_obj_set_width(lblc, lblc_width);
            lv_obj_set_grid_cell(lblc, LV_GRID_ALIGN_STRETCH, i + 1, 1, LV_GRID_ALIGN_STRETCH, j + 1, 1);
            lv_obj_center(lblc);
            lv_obj_set_style_text_align(lblc, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
            coord_val_labels[coord_systems[i]].push_back(lblc);
        }
    }

    interface->machine->addPosChangedCb([this](MachineInterface* mach) { _pos_updated(mach); });
    interface->machine->addHomeChangedCb([this](MachineInterface* mach) { _home_updated(mach); });
    interface->machine->addWcsChangedCb([this](MachineInterface* mach) { _wcs_updated(mach); });

    _wcs_updated(interface->machine);
}

void MachinePositionWCS::coordsUndefined() {
    for (const auto& cs : coord_systems) {
        coord_vals[cs] = std::vector<float>(coords.size(), NAN);
        for (lv_obj_t* lblc : coord_val_labels[cs]) {
            std::string q_marks(digits - 2, '?');
            lv_label_set_text(lblc, (q_marks + ".??").c_str());
        }
    }
}

void MachinePositionWCS::setCoord(int ax, float v, const std::string& coord_system) {
    std::string cs = coord_system.empty() ? coord_systems[0] : coord_system;

    if (ax < 0 || ax >= coords.size()) {
        Serial.println("Axis index out of bounds!");
        return;
    }

    if (!interface->machine->axes_homed[ax]) {
        v = NAN;
    }

    if (isnan(v) && isnan(coord_vals[cs][ax])) return;  // both are NAN, no update needed
    if (!isnan(v) && !isnan(coord_vals[cs][ax]) && abs(v - coord_vals[cs][ax]) < 0.0001) return; //values are close enough

    coord_vals[cs][ax] = v;

    char buf[32];
    if (!isnan(v)) {
        snprintf(buf, sizeof(buf), fmt_str.c_str(), v);
        lv_label_set_text(coord_val_labels[cs][ax], buf);
    } else {
        std::string q_marks(digits - 2, '?');
        lv_label_set_text(coord_val_labels[cs][ax], (q_marks + ".??").c_str());
    }
}

void MachinePositionWCS::setCoord(const std::string& ax, float v, const std::string& coord_system) {
    auto it = std::find(coords.begin(), coords.end(), ax);
    if (it != coords.end()) {
        setCoord(std::distance(coords.begin(), it), v, coord_system);
    } else {
        Serial.println("Invalid axis name!");
    }
}


void MachinePositionWCS::_label_home_clicked(lv_event_t* e) {
    lv_obj_t* label = (lv_obj_t *) lv_event_get_target(e);
    std::string ax_text = lv_label_get_text(label);
    std::string ax = axis_label_ids[ax_text];
    interface->machine->home(ax);
}

void MachinePositionWCS::_pos_updated(MachineInterface* mach) {
    // TODO: Machine, WCS and remaining coord systems currently hard-coded.
    for (size_t i = 0; i < mach->position.size(); ++i) {
        setCoord(i, mach->position[i], coord_systems[0]);
    }
    if (coord_systems.size() > 1) {
        for (size_t i = 0; i < mach->wcs_position.size(); ++i) {
            setCoord(i, mach->wcs_position[i], coord_systems[1]);
            float pos = mach->position[i];
            float coord = mach->wcs_position[i];
            if (coord_systems.size() > 2 && !isnan(pos) && !isnan(coord)) {
                float diff = coord - pos;
                setCoord(i, diff, coord_systems[2]);
            }
        }
    }
}

void MachinePositionWCS::_home_updated(MachineInterface* mach) {
    for (size_t i = 0; i < mach->axes_homed.size(); ++i) {
        lv_color_t colr = t_color(mach->axes_homed[i] ? color("BLUE") : color("RED"));
        lv_obj_set_style_bg_color(axis_labels[i], colr, LV_STATE_DEFAULT);
    }
    _pos_updated(mach);
}

void MachinePositionWCS::_wcs_updated(MachineInterface* mach) {
    std::string t = mach->getWcsStr();
    lv_label_set_text(coord_sys_labels[1], t.c_str());
}


MachinePosition::MachinePosition(lv_obj_t* parent, const std::vector<std::string>& coords,
                       Interface* interface, int digits) {
        this->self = lv_obj_create(parent);
        this->interface = interface;
        this->parent = parent;
        this->coords = coords;
        this->digits = digits;
        char fmt[20];
        sprintf(fmt, "%%%d.2f", digits - 2);
        this->fmt_str = fmt;

        this->container = create_container(parent);
        flex_row(container, 0, 0, false);
        lv_obj_set_style_pad_column(container, 0, LV_STATE_DEFAULT);
        non_scrollable(container);

        coord_vals = std::vector<float>(coords.size(), NAN);

        lv_font_t* font = this->interface->font_lcd_18 ? this->interface->font_lcd_18 : this->interface->font_lcd;
        for (const auto& coord : coords) {
            lv_obj_t* lbl = lv_label_create(container);
            std::string text = coord + " ";
            lv_label_set_text(lbl, text.c_str());
            style(lbl, {{"bg_color", color("BLUE")}, {"bg_opa", 100}, {"margin", 1}, {"padding", 0}});

            lv_obj_t* lblc = lv_label_create(container);
            std::string q_marks(digits - 2, '?');
            lv_label_set_text(lblc, (q_marks + ".??").c_str());
            style(lblc, {{"bg_color", color("LIME")}, {"bg_opa", 100}, {"margin", 0}, {"padding", 2}});

            if (font != nullptr) {
                lv_obj_set_style_text_font(lblc, font, LV_STATE_DEFAULT);
            }
            lv_obj_set_width(lblc, (digits + 1) * FONT_WIDTH);

            coord_labels.push_back(lblc);
        }
}


void MachinePosition::coordsUndefined(){
    coord_vals = std::vector<float>(coords.size(), NAN);
    for (lv_obj_t* lblc : coord_labels) {
            std::string q_marks(digits - 2, '?');
            lv_label_set_text(lblc, (q_marks + ".??").c_str());
    }
}


void MachinePosition::setCoord_(int c, float v) {
    if (c < 0 || c >= coords.size()) {
        Serial.println("Axis index out of bounds!");
        return;
    }

    if (isnan(v) && isnan(coord_vals[c])) return;  // both are NAN, no update needed
    if (!isnan(v) && !isnan(coord_vals[c]) && abs(v - coord_vals[c]) < 0.0001) return; //values are close enough

    coord_vals[c] = v;

    char buf[32];
    if (!isnan(v)) {
        snprintf(buf, sizeof(buf), fmt_str.c_str(), v);
        lv_label_set_text(coord_labels[c], buf);
    } else {
        std::string q_marks(digits - 2, '?');
        lv_label_set_text(coord_labels[c], (q_marks + ".??").c_str());
    }
}

void MachinePosition::setCoord_(const std::string& c, float v) {
        auto it = std::find(coords.begin(), coords.end(), c);
    if (it != coords.end()) {
        setCoord_(std::distance(coords.begin(), it), v);
    } else {
        Serial.println("Invalid axis name!");
    }
}