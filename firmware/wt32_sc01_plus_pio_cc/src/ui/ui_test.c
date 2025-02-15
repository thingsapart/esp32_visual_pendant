#include "ui_helpers.h"

#include <stdio.h>

void test_ui(lv_obj_t *screen) {
    init_object_registry();

     // --- Flex Example ---
    CONT("flex_container", screen,
        _size(obj, 300, 200);
        _align(obj, LV_ALIGN_TOP_MID);
        _pad_top(obj, 20, LV_PART_MAIN);
        _layout(obj, LV_LAYOUT_FLEX);
        _flex_row_wrap(obj);       // Enable wrapping
        _flex_space_between(obj);  // Space between items
    );
    
    lv_obj_t *flex_cont = get_object_by_id("flex_container");

    for (int i = 0; i < 5; i++) {
        char btn_id[20];
        snprintf(btn_id, sizeof(btn_id), "flex_btn_%d", i);
        BTN(btn_id, flex_cont,
            _size(obj, 80, 40);
             //_flex_grow(obj, 1); // Example: Make items grow equally (commented out)
             _bg_color(obj, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
              lv_obj_t * btn_label = lv_label_create(obj);
             _label_text(btn_label, "Btn");
             lv_obj_center(btn_label);
        );
    }

   // --- Grid Example ---
    CONT("grid_container", screen,
        _size(obj, 300, 200);
        _align(obj, LV_ALIGN_BOTTOM_MID);
        _pad_bottom(obj, 50, LV_PART_MAIN);
        _layout(obj, LV_LAYOUT_GRID);

        _grid_template(obj, _cols(100, 100, 100, 0), _rows(60, 60, 0));

        _grid_align_stretch_all(obj);  // Stretch items to fill cells
        
    );
    lv_obj_t * grid_cont = get_object_by_id("grid_container");

    // Add items to the grid
    LABEL("grid_label_1", grid_cont,
        _label_text(obj, "Cell 1");
        _bg_color(obj, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
         _grid_cell(obj, 0, 1, 0, 1); // col_start, col_end, row_start, row_end
    );

    BTN("grid_btn_1", grid_cont,
        _bg_color(obj, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        _grid_cell(obj, 1, 3, 0, 1); // Span 2 columns
          lv_obj_t * btn_label = lv_label_create(obj);
        _label_text(btn_label, "Button");
        lv_obj_center(btn_label);
    );

    IMG("grid_img_1", grid_cont,
        // Assuming you have an image asset declared elsewhere (e.g., LV_IMG_DECLARE(my_image))
        // _img_src(obj, &my_image); // You would set your image source here.
         lv_img_set_src(obj, LV_SYMBOL_OK); // Use a built-in symbol as a placeholder
        _grid_cell(obj, 0, 2, 1, 2); // Span 2 columns and 1 row
    );
}