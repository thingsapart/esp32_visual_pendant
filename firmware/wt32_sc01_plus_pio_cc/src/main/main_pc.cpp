#ifdef POSIX

#define MACHINE_POLL_INTERVAL 500
#define RRF_SIM 1

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "SDL2/SDL.h"
#include "lvgl.h"
#include "tasks/machine_task.h"

// #include "Arduino.h"

volatile sig_atomic_t bRunning = false;

void signal_handler(int interrupt) {
  printf("captured interrupt %d\r\n", interrupt);
  if (interrupt == SIGINT) {
    bRunning = false;
  }
}

#ifdef __cplusplus
extern "C" {
#endif
void encoder_set_ui_mode() {}

void encoder_set_encoder_mode() {}
#ifdef __cplusplus
}
#endif

#define RRF_SIM 1

#include "config.h"
#include "debug.h"
#include "driver/arduino_serial_wrapper.h"
#include "machine/machine_interface.h"
#include "machine/machine_sim.h"
#include "ui/interface.h"

static duet_simulator_t *machine;
static interface_t interface;

thrd_t machine_sim_task;

#include <map>
#ifdef __cplusplus
extern "C" {
#endif

#include <tasks/machine_task.h>

#include "driver/arduino_serial_wrapper.h"

#if 0
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

#endif
#ifdef __cplusplus
}
#endif

static lv_display_t *lvDisplay;
static lv_indev_t *lvMouse;
static lv_indev_t *lvMouseWheel;
static lv_indev_t *lvKeyboard;

void lvgl_sdl() {
  lv_init();

// Workaround for sdl2 `-m32` crash
// https://bugs.launchpad.net/ubuntu/+source/libsdl2/+bug/1775067/comments/7
#ifndef WIN32
  setenv("DBUS_FATAL_WARNINGS", "0", 1);
#endif

#if LV_USE_LOG != 0
  lv_log_register_print_cb(lv_log_print_g_cb);
#endif

  /* Add a display
   * Use the 'monitor' driver which creates window on PC's monitor to simulate a
   * display*/
  lvDisplay = lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES);
  lv_sdl_window_set_title(lvDisplay, "Pendant Simulator");
  lvMouse = lv_sdl_mouse_create();
  lvMouseWheel = lv_sdl_mousewheel_create();
  lvKeyboard = lv_sdl_keyboard_create();

  lv_sdl_window_set_zoom(lvDisplay, 1);

  lv_tick_set_cb(SDL_GetTicks);

  signal(SIGINT, signal_handler);

  // start the UI
  machine = duet_simulator_create(MACHINE_POLL_INTERVAL);

  interface_init(&interface, &machine->base);

  // test_screen();
  // interface = new Interface(machine);
  _d(0, "LOADED..\n");

  bRunning = true;

  lv_tick_set_cb(SDL_GetTicks);
  if (!machine_task_run("MachineSim", &machine->base, &machine_sim_task)) {
    bRunning = false;
    printf("Failed to create machine sim task");
  }

  while (bRunning) {
    lv_timer_handler();  // Update the UI-

    // task handler
    lv_task_handler();

    // Update display
    interface_tick(&interface);
  }

  lv_sdl_quit();
}

// Not used this is here still for reference of a simulator loop with sdl
void lvgl_sdl_prev() {
  lv_init();

  int screen_width = 640;
  int screen_height = 480;

  lv_display_t *disp;
  disp = lv_sdl_window_create(screen_width, screen_height);
  lv_sdl_window_set_zoom(disp, 1);

  lv_sdl_window_set_title(disp, "LVGL v9.2.2");
  // lv_theme_t *theme = lv_theme_default_init(NULL,
  // lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false,
  // LV_FONT_DEFAULT); lv_disp_set_theme(disp, theme);

  lv_indev_t *mouse = lv_sdl_mouse_create();
  lv_indev_t *keyboard = lv_sdl_keyboard_create();
  lv_indev_t *mousewheel = lv_sdl_mousewheel_create();

  lv_tick_set_cb(SDL_GetTicks);

  signal(SIGINT, signal_handler);

  machine = duet_simulator_create(MACHINE_POLL_INTERVAL);

  // start the UI
  // machine = duet_simulator_create(50);

  interface_init(&interface, &machine->base);

  // test_screen();
  // interface = new Interface(machine);
  _d(0, "LOADED..\n");

  bRunning = true;
  uint32_t last_tick = SDL_GetTicks();
  while (bRunning) {
    uint32_t current_tick = SDL_GetTicks();
    uint32_t elapsed = current_tick - last_tick;
    last_tick = current_tick;

    // task handler
    lv_task_handler();

    uint32_t sleep_time = (1000 / 60) - elapsed;
    if (sleep_time < 0) {
      usleep(sleep_time * 1000);
    }
  }

  lv_sdl_quit();
}

int main(int argc, char **argv) {
  lvgl_sdl();

  return 0;
}

#endif