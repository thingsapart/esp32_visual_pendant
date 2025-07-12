#include "lvgl.h"
#include "ui/layout/lv_vfl.h"
#include "ui/layout/lv_views.h"

lv_obj_t* create_feedrate_view(lv_obj_t* parent) {
#if 0
    // Declare pointers for the widgets we might need to access later
    lv_obj_t *cont = NULL;
    lv_obj_t *lbl_f = NULL;
    lv_obj_t *cont_tr = NULL;
    lv_obj_t *bar_feed = NULL;
    lv_obj_t *lbl_feed_title = NULL;
    lv_obj_t *lbl_feed_unit = NULL;
    lv_obj_t *lbl_feed_value = NULL;
    lv_obj_t *lbl_fov_title = NULL; // FOV = Feed Override Value?
    lv_obj_t *lbl_fov_value = NULL;
    lv_obj_t *lbl_sov_value = NULL; // SOV = Spindle Override Value? Let's call it SOV based on common CNC terms. The image shows '70%'.

    // Create the main container for this section using a basic obj
    // The view macro will handle creating children inside this container.
    cont = lv_obj_create(parent);

    // Use the view macro to define the layout and widgets
    view(cont,
        // --- Container Settings ---
        _size(LV_PCT(100), LV_SIZE_CONTENT), // Take full width, height based on content
        _bg_color(lv_color_hex(0x202020)),   // Dark background matching image
        _bg_opa(LV_OPA_COVER),
        _pad_all(5),                        // Add a little padding around the whole element
        _border_width(0),                   // No border on the main container
        _radius(0),                         // No radius on the main container

        // --- Widgets ---

        // Top Right: FEED MM/MIN (in a sub-container for horizontal layout)
        obj(cont_tr,
            _bg_opa(LV_OPA_TRANSP), _pad_all(0), _border_width(0), // Make sub-container invisible
            _layout_h(cont_tr, LV_ALIGN_MIDDLE, // Use horizontal flex layout inside
                _flex_main_place(LV_FLEX_ALIGN_SPACE_BETWEEN), // Push FEED left, MM/MIN right
                label(lbl_feed_title, "FEED",
                    _text_color(lv_color_hex(0xAAAAAA)),
                    _text_font(&lv_font_montserrat_12) // Small font
                ),
                label(lbl_feed_unit, "MM/MIN",
                    _text_color(lv_color_hex(0xAAAAAA)),
                    _text_font(&lv_font_montserrat_12) // Small font
                )
            )
        ),

        // Middle Left: Large "F"
        label(lbl_f, "F",
            _cell(_cell_opts(.col=0, .row=1, .col_align=LV_GRID_ALIGN_END, .row_align=LV_GRID_ALIGN_CENTER)),
            _text_color(lv_color_white()),
            _text_font(&lv_font_montserrat_40) // Large font (ensure this exists)
        ),

        // Middle Right: Large "100" Value
        label(lbl_feed_value, "100",
            _cell(_cell_opts(.col=1, .row=1, .col_align=LV_GRID_ALIGN_CENTER, .row_align=LV_GRID_ALIGN_CENTER)),
            _text_color(lv_color_white()),
            _text_font(&lv_font_montserrat_40) // Large font
        ),

        // Bottom Left: Feed Override Bar
        bar(bar_feed,
            _cell(_cell_opts(.col=0, .row=2, .col_align=LV_GRID_ALIGN_STRETCH, .row_align=LV_GRID_ALIGN_CENTER)), // Span full cell width, centered vertically
            _size(LV_PCT(90), 15),              // 90% width of its cell, 15px height
            _align(LV_ALIGN_CENTER, 0, 0),      // Center within the cell (grid alignment also helps)
            _bar_range(0, 200),                 // Example Range (adjust as needed)
            _bar_value(100, LV_ANIM_OFF),       // Set value to 100% (matching label initially)
            _bar_mode(LV_BAR_MODE_NORMAL),
            _radius(3),                         // Slightly rounded ends for the bar
            _bg_color(lv_color_hex(0x3A3A3A)),  // Dark background for the bar trough
            _bg_opa(LV_OPA_COVER),
            selector(LV_PART_INDICATOR,         // Style the indicator part
                _radius(3),                     // Match bar radius
                _bg_opa(LV_OPA_COVER),
                _bg_grad_color(lv_color_hex(0xFFB000)), // Gradient End: Orange/Yellow
                _bg_color(lv_color_hex(0x00D0FF)),      // Gradient Start: Cyan/Light Blue
                _bg_grad_dir(LV_GRAD_DIR_HOR),  // Horizontal gradient
                _bg_main_stop(0),               // Gradient starts at the beginning
                _bg_grad_stop(255)              // Gradient ends at the end
            )
        ),

        // Bottom Right: FOV/SOV Labels (in a sub-container for vertical layout)
        obj(cont_br,
            _cell(_cell_opts(.col=1, .row=2, .col_align=LV_GRID_ALIGN_END, .row_align=LV_GRID_ALIGN_CENTER)), // Align container to right|center
            _bg_opa(LV_OPA_TRANSP), _pad_all(0), _border_width(0), // Make sub-container invisible
            _layout_v(LV_ALIGN_TOP, // Use vertical flex layout inside
                _flex_cross_place(LV_FLEX_ALIGN_END), // Align items inside to the right
                _flex_main_place(LV_FLEX_ALIGN_START),// Stack items from the top
                _pad_row(2), // Small gap between labels
                 _width(LV_SIZE_CONTENT), // Container shrinks to fit content width

                label(lbl_fov_title, "FOV", // Feed Override Title
                    _text_color(lv_color_hex(0xAAAAAA)),
                    _text_font(&lv_font_montserrat_12)
                ),
                label(lbl_fov_value, "155%", // Feed Override Value
                    _text_color(lv_color_white()),
                    _text_font(&lv_font_montserrat_12) // Using small font as per image
                ),
                 label(lbl_sov_value, "70%", // Spindle Override Value?
                    _text_color(lv_color_white()),
                    _text_font(&lv_font_montserrat_12) // Using small font as per image
                )
            )
        )
    ); // End view macro

    // --- Grid Layout ---
    // Define a 2-column, 3-row grid structure.
    _layout_grid(container,
        // Columns: Left approx 40%, Right approx 60%
        _cols(_fr(2), _fr(3)),
        // Rows: Top labels, Main F/100, Bar/Bottom labels
        _rows(_content(), _content(), _content()),


        _cell(cont_tr, _cell_opts(.col=1, .row=0, .col_align=LV_GRID_ALIGN_STRETCH, .row_align=LV_GRID_ALIGN_CENTER)),

        _grid_column_gap(8), // Space between columns
        _grid_row_gap(4)     // Space between rows
    )


    return container;
#endif
  return NULL;
}

// Example Usage:
// void my_ui_init(void) {
//     lv_obj_t * screen = lv_screen_active();
//     // Create a placeholder container if needed, e.g., to simulate the
//     240x320 context lv_obj_t * main_container = lv_obj_create(screen);
//     lv_obj_set_size(main_container, 240, 80); // Approximate size from image
//     lv_obj_center(main_container);
//     lv_obj_set_style_bg_color(main_container, lv_color_hex(0x111111), 0); //
//     Darker background for context lv_obj_set_style_pad_all(main_container, 0,
//     0); lv_obj_set_style_border_width(main_container, 0, 0);
//
//     lv_obj_t* feed_widget = create_feedrate_widget(main_container);
//     // You might align feed_widget within main_container if needed,
//     // but since it's set to LV_PCT(100) width, it should fill horizontally.
//     lv_obj_align(feed_widget, LV_ALIGN_TOP_MID, 0, 0); // Align it at the top
// }