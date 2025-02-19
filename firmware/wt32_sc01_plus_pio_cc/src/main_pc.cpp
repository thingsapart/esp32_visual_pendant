#ifdef POSIX

#include <stdio.h>
#include <signal.h>

#include <unistd.h>

#include "lvgl.h"
#include "SDL2/SDL.h"

#include "Arduino.h"

volatile sig_atomic_t bRunning = false;

void signal_handler(int interrupt)
{
    printf("captured interrupt %d\r\n", interrupt);
    if (interrupt == SIGINT)
    {
        bRunning = false;
    }
}

#include "machine/machine_interface.h"
#include "machine/machine_sim.h"
#include "ui/interface.h"
#include "debug.h"

static duet_simulator_t *machine;
static interface_t interface;

#include <map>

extern "C" {

#include "machine/arduino_serial_wrapper.h"

serial_handle_t serial_init(uint8_t uart_num, unsigned long baud, serial_config_t config, int8_t rx_pin, int8_t tx_pin) {
    return NULL;
}

void serial_end(serial_handle_t handle) {
}

size_t serial_write(serial_handle_t handle, const uint8_t *buffer, size_t size) {
   char buf[size+1];
   strncpy(buf, (char *) buffer, size);
   buf[size] = '\0';
   printf("%s", buf);
   return size;
}

int serial_read(serial_handle_t handle) {
    return 0;
}

size_t serial_available(serial_handle_t handle) {
    return 0;
}

size_t serial_available_for_write(serial_handle_t handle) {
    return 0;
}

int serial_peek(serial_handle_t handle) {
    return 0;
}

void serial_flush(serial_handle_t handle) {
}

size_t serial_read_bytes(serial_handle_t handle, uint8_t *buffer, size_t length) {
    return 0;
}

static char *empty = { 0 };
char *serial_read_line(serial_handle_t handle) {
    return empty;
}

size_t serial_read_line_buf(serial_handle_t handle, char *buf, size_t len, long timeout_ms) {
    return 0;
}

}

void lvgl_test()
{
    lv_init();

    int screen_width = 640;
    int screen_height = 480;

    lv_display_t *disp;
    disp = lv_sdl_window_create(screen_width, screen_height);
    lv_sdl_window_set_zoom(disp, 1);

    lv_sdl_window_set_title(disp, "LVGL v9.2.2");
    //lv_theme_t *theme = lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    //lv_disp_set_theme(disp, theme);

    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_t *keyboard = lv_sdl_keyboard_create();
    lv_indev_t *mousewheel = lv_sdl_mousewheel_create();

    lv_tick_set_cb(SDL_GetTicks);

    signal(SIGINT, signal_handler);

    // start the UI
    machine = duet_simulator_create(50);
    interface_init(&interface, &machine->base);
    
    // test_screen();
    //interface = new Interface(machine);
    _d(0, "LOADED..\n");

    bRunning = true;
    uint32_t last_tick = SDL_GetTicks();
    while (bRunning)
    {
        uint32_t current_tick = SDL_GetTicks();
        uint32_t elapsed = current_tick - last_tick;
        last_tick = current_tick;

        // task handler
        lv_task_handler();

        uint32_t sleep_time = (1000 / 60) - elapsed;
        if(sleep_time < 0)
        {
            usleep(sleep_time * 1000);
        }
    }
    
    lv_sdl_quit();
}

int main(int argc, char **argv)
{
    lvgl_test();
    return 0;
}

#endif