#include "ui_setup_dwc.h"

#ifdef DWC_MACHINE_MODE

#include <stdio.h>
#include <string.h>

#include "config/dwc_settings.h"
#include "driver/dwc_http_client_wrapper.h"
#include "driver/mdns_wrapper.h"
#include "driver/wifi_manager.h"
#include "esp_system.h" // For esp_restart
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- State for the setup flow ---
static int wifi_fail_count = 0;
static char last_failed_ssid[33] = {0};
#define MAX_WIFI_FAIL_COUNT 3

// --- Forward Declarations ---
static void host_entry_event_handler(lv_event_t* e);
static void create_host_modal(interface_t* interface, const char* error_msg);
static void password_entry_event_handler(lv_event_t* e);
static void create_password_modal(interface_t* interface);
static void ssid_list_event_handler(lv_event_t* e);
static void create_ssid_modal(interface_t* interface, const char* ssids);
static void on_wifi_scan_done(const char* scan_results, void* user_data);
static void create_wifi_fail_modal(interface_t* interface);
static void create_wifi_total_fail_modal(interface_t* interface);


void ui_start_dwc_setup_flow(interface_t* interface) {
    lv_obj_t* msg = lv_msgbox_create(NULL);
    lv_msgbox_add_text(msg, "Scanning for Wi-Fi networks...");
    lv_obj_center(msg);
    lv_refr_now(NULL);

    wifi_manager_scan(on_wifi_scan_done, interface);

    lv_obj_del(msg);
}

static void on_wifi_scan_done(const char* scan_results, void* user_data) {
    create_ssid_modal((interface_t*)user_data, scan_results);
}

static void create_ssid_modal(interface_t* interface, const char* ssids) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Select Wi-Fi Network");
    lv_obj_t* content = lv_msgbox_get_content(mbox);
    lv_obj_t* dd = lv_dropdown_create(content);
    lv_dropdown_set_options(dd, ssids);
    lv_obj_add_event_cb(dd, ssid_list_event_handler, LV_EVENT_VALUE_CHANGED, interface);
    lv_obj_center(mbox);
}

static void ssid_list_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);
    interface_t* interface = (interface_t*)lv_event_get_user_data(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        char buf[33];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        strncpy(interface->setup_settings.ssid, buf, sizeof(interface->setup_settings.ssid) - 1);
        
        lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(obj));
        lv_msgbox_close(mbox);

        create_password_modal(interface);
    }
}

static void create_password_modal(interface_t* interface) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    char title[64];
    snprintf(title, sizeof(title), "Password for %s", interface->setup_settings.ssid);
    lv_msgbox_add_title(mbox, title);

    lv_obj_t* content = lv_msgbox_get_content(mbox);
    lv_obj_t* ta = lv_textarea_create(content);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_obj_set_width(ta, lv_pct(100));

    lv_obj_t* kb = lv_keyboard_create(lv_layer_top());
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, password_entry_event_handler, LV_EVENT_ALL, interface);

    lv_obj_add_flag(mbox, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(mbox, LV_ALIGN_TOP_MID, 0, 20);
}

static void password_entry_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* kb = lv_event_get_target(e);
    interface_t* interface = (interface_t*)lv_event_get_user_data(e);

    lv_obj_t* ta = lv_keyboard_get_textarea(kb);
    lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(ta));

    if (lv_keyboard_get_btn_text(kb, lv_keyboard_get_selected_btn(kb)) == LV_SYMBOL_OK) {
        strncpy(interface->setup_settings.password, lv_textarea_get_text(ta), sizeof(interface->setup_settings.password) - 1);
        lv_msgbox_close(mbox);
        lv_obj_del(kb);

        if (strcmp(last_failed_ssid, interface->setup_settings.ssid) != 0) {
            strcpy(last_failed_ssid, interface->setup_settings.ssid);
            wifi_fail_count = 0;
        }
        wifi_fail_count++;

        lv_obj_t* connecting_mbox = lv_msgbox_create(NULL);
        lv_msgbox_add_text(connecting_mbox, "Connecting to Wi-Fi...");
        lv_obj_center(connecting_mbox);
        lv_refr_now(NULL);

        bool connected = wifi_manager_connect(interface->setup_settings.ssid, interface->setup_settings.password, 15000);

        lv_obj_del(connecting_mbox);

        if (connected) {
            wifi_fail_count = 0;
            last_failed_ssid[0] = '\0';
            create_host_modal(interface, NULL);
        } else {
            if (wifi_fail_count >= MAX_WIFI_FAIL_COUNT) {
                create_wifi_total_fail_modal(interface);
            } else {
                create_wifi_fail_modal(interface);
            }
        }

    } else if (lv_keyboard_get_btn_text(kb, lv_keyboard_get_selected_btn(kb)) == LV_SYMBOL_CLOSE) {
        lv_msgbox_close(mbox);
        lv_obj_del(kb);
    }
}


static void create_host_modal(interface_t* interface, const char* error_msg) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Enter DWC Hostname or IP");

    lv_obj_t* content = lv_msgbox_get_content(mbox);

    if (error_msg) {
        lv_obj_t* err_label = lv_label_create(content);
        lv_label_set_text(err_label, error_msg);
        lv_obj_set_style_text_color(err_label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_width(err_label, lv_pct(100));
        lv_label_set_long_mode(err_label, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t* ta = lv_textarea_create(content);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, lv_pct(100));

    lv_obj_t* kb = lv_keyboard_create(lv_layer_top());
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, host_entry_event_handler, LV_EVENT_ALL, interface);
    
    lv_obj_add_flag(mbox, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(mbox, LV_ALIGN_TOP_MID, 0, 20);
}

static void host_entry_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* kb = lv_event_get_target(e);
    interface_t* interface = (interface_t*)lv_event_get_user_data(e);
    
    lv_obj_t* ta = lv_keyboard_get_textarea(kb);
    lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(ta));

    if (lv_keyboard_get_btn_text(kb, lv_keyboard_get_selected_btn(kb)) == LV_SYMBOL_OK) {
        strncpy(interface->setup_settings.host, lv_textarea_get_text(ta), sizeof(interface->setup_settings.host) - 1);
        lv_msgbox_close(mbox);
        lv_obj_del(kb);

        lv_obj_t* connecting_mbox = lv_msgbox_create(NULL);
        lv_msgbox_add_text(connecting_mbox, "Testing host...");
        lv_obj_center(connecting_mbox);
        lv_refr_now(NULL);

        // --- Host resolution and connection test ---
        char host_to_use[65] = {0};
        char resolved_ip_str[16] = {0};
        bool is_ip_format = (strspn(interface->setup_settings.host, "0123456789.") == strlen(interface->setup_settings.host));
        
        lv_msgbox_add_text(connecting_mbox, is_ip_format ? "Testing host connection..." : "Resolving hostname...");
        lv_refr_now(NULL);

        if(!wifi_manager_resolve_host(interface->setup_settings.host, host_to_use, sizeof(host_to_use))) {
            lv_obj_del(connecting_mbox);
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), "Error: Could not resolve hostname\n%s", interface->setup_settings.host);
            create_host_modal(interface, error_msg);
            return; // Stop here
        }
        
        if (!is_ip_format) {
             strncpy(resolved_ip_str, host_to_use, sizeof(resolved_ip_str) - 1);
        }

        dwc_http_handle_t client = dwc_http_create(host_to_use, 80);
        bool host_ok = false;
        int read_len = -1;
        int status_code = 0;
        if (client) {
            char response_buffer[32];
            read_len = dwc_http_get(client, "/rr_status?type=1", response_buffer, sizeof(response_buffer), &status_code);
            if (read_len >= 0 && (status_code == 200 || status_code == 401)) { // 401 is also ok, it means server is there
                host_ok = true;
            }
            dwc_http_destroy(client);
        }
        
        lv_obj_del(connecting_mbox);

        if (host_ok) {
            if (dwc_settings_save(&interface->setup_settings)) {
                lv_obj_t* reboot_mbox = lv_msgbox_create(NULL);
                lv_msgbox_add_text(reboot_mbox, "Settings saved! Rebooting...");
                lv_obj_center(reboot_mbox);
                lv_refr_now(NULL);
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            } else {
                create_host_modal(interface, "Error: Could not save settings.");
            }
        } else {
            char error_msg[256];
            char full_url[128];
            snprintf(full_url, sizeof(full_url), "http://%s/rr_status?type=1", host_to_use);
            if (!client) {
                 snprintf(error_msg, sizeof(error_msg), "Error: Invalid host format.\nTesting: %s", interface->setup_settings.host);
            } else if (read_len < 0) {
                if (is_ip_format) {
                     snprintf(error_msg, sizeof(error_msg), "Error: Connection failed.\n- Check host/IP & network.\n- Check port 80 is open.\n\nURL: %s", full_url);
                } else {
                    snprintf(error_msg, sizeof(error_msg), "Error: Connection failed.\n- Host '%s' resolved to %s.\n- Check host/IP & port 80.\n\nURL: %s", interface->setup_settings.host, resolved_ip_str, full_url);
                }
            } else { // HTTP error
                snprintf(error_msg, sizeof(error_msg), "Error: Host responded with HTTP %d.\n- Check DWC is running.\n\nURL: %s", status_code, full_url);
            }
            create_host_modal(interface, error_msg);
        }

    } else if (lv_keyboard_get_btn_text(kb, lv_keyboard_get_selected_btn(kb)) == LV_SYMBOL_CLOSE) {
        lv_msgbox_close(mbox);
        lv_obj_del(kb);
    }
}


static void wifi_fail_modal_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code != LV_EVENT_CLICKED) return;

    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * mbox = lv_obj_get_parent(lv_obj_get_parent(btn));
    interface_t* interface = (interface_t*)lv_event_get_user_data(e);
    const char* btn_text = lv_label_get_text(lv_obj_get_child(btn, 0));
    
    if(!btn_text) return;

    lv_msgbox_close(mbox);

    if (strcmp(btn_text, "Change Password") == 0) {
        create_password_modal(interface);
    } else if (strcmp(btn_text, "Choose Wi-Fi") == 0) {
        ui_start_dwc_setup_flow(interface);
    }
}

static void create_wifi_fail_modal(interface_t* interface) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Connection Failed");
    lv_msgbox_add_text(mbox, "Could not connect to the Wi-Fi network. Please check the password and try again.");
    lv_obj_t* btn1 = lv_msgbox_add_footer_button(mbox, "Change Password");
    lv_obj_t* btn2 = lv_msgbox_add_footer_button(mbox, "Choose Wi-Fi");
    lv_obj_add_event_cb(btn1, wifi_fail_modal_event_handler, LV_EVENT_CLICKED, interface);
    lv_obj_add_event_cb(btn2, wifi_fail_modal_event_handler, LV_EVENT_CLICKED, interface);
    lv_obj_center(mbox);
}

static void wifi_total_fail_modal_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    lv_obj_t* btn = lv_event_get_target(e);
    interface_t* interface = (interface_t*)lv_event_get_user_data(e);
    const char* btn_text = lv_label_get_text(lv_obj_get_child(btn, 0));

    lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent(btn)));

    if (strcmp(btn_text, "Restart Setup") == 0) {
        wifi_fail_count = 0;
        last_failed_ssid[0] = '\0';
        ui_start_dwc_setup_flow(interface);
    }
}

static void create_wifi_total_fail_modal(interface_t* interface) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Repeated Failures");
    lv_msgbox_add_text(mbox, "Failed to connect multiple times.\n\nPlease check your router and Wi-Fi credentials.");
    lv_obj_t* btn1 = lv_msgbox_add_footer_button(mbox, "Restart Setup");
    lv_obj_t* btn2 = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(btn1, wifi_total_fail_modal_event_handler, LV_EVENT_CLICKED, interface);
    lv_obj_add_event_cb(btn2, wifi_total_fail_modal_event_handler, LV_EVENT_CLICKED, interface);
    lv_obj_center(mbox);
}

#endif // DWC_MACHINE_MODE

