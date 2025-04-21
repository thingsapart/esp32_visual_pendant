// #include "ui/layout/lv_view_def_cons.h"

def_view(bar_value_view,
    components(
        container(header_cont,
            component(caption, label, style(header_cont, __text_color(lv_color_white()))),
        ),
        container(main_cont,
            component(abbreviation, label, style(header_cont, __text_color(lv_color_white()))),
            container(div_val_bars,
                component(value, label),
                component(bar_utilization, bar),
                component(scale_utilization, scale),
            ),
            container(div_status,
                component(unit, label),
                component(rate_override, label),
                component(utilization, label),
            )
        )
    ),

    layout(
        _layout_v(self->div_val_bars, LV_FLEX_ALIGN_START,
            _flex(self->value, 1),
            _fixed(self->bar_utilization, 20),
            _fixed(self->scale_utilization, 10)
        ),
        _layout_v(self->div_status, LV_FLEX_ALIGN_START,
            _content(self->unit),
            _content(self->rate_override),
            _content(self->utilization)
        ),

        _layout_v(self->main, LV_FLEX_ALIGN_START,
            _fixed(self->header_cont, 16),
            _flex(self->main_cont, 1),
        ),
    )

    style(self->header_cont,
        __width(lv_pct(100))
    )
);