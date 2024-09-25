/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Include necessary ZMK headers
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>

#include "output_status.h"

// Initialize a static system slist for widgets
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// Declare LVGL image symbols
LV_IMG_DECLARE(sym_usb);
LV_IMG_DECLARE(sym_bt);
LV_IMG_DECLARE(sym_ok);
LV_IMG_DECLARE(sym_nok);
LV_IMG_DECLARE(sym_open);
LV_IMG_DECLARE(sym_1);
LV_IMG_DECLARE(sym_2);
LV_IMG_DECLARE(sym_3);
LV_IMG_DECLARE(sym_4);
LV_IMG_DECLARE(sym_5);

// Array of pointers to number symbols
const lv_img_dsc_t *sym_num[] = {
    &sym_1,
    &sym_2,
    &sym_3,
    &sym_4,
    &sym_5
};

// Enum for output symbols
enum output_symbol {
    output_symbol_usb,
    output_symbol_usb_hid_status,
    output_symbol_bt,
    output_symbol_bt_number,
    output_symbol_bt_status,
    output_symbol_selection_line
};

// Enum for selection line state
enum selection_line_state {
    selection_line_state_usb,
    selection_line_state_bt
} current_selection_line_state;

// Define selection line points
lv_point_t selection_line_points[] = { {-1, 0}, {12, 0} }; // will be replaced with lv_point_precise_t

// Structure to hold the output status state
struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool usb_is_hid_ready;
};

// Function to get the current output status state
static struct output_status_state get_state(const zmk_event_t *eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
        .usb_is_hid_ready = zmk_usb_is_hid_ready()
    };
}

// Animation callback for x-coordinate
static void anim_x_cb(void *var, int32_t v) {
    lv_obj_set_x(var, v);
}

// Animation callback for size
static void anim_size_cb(void *var, int32_t v) {
    selection_line_points[1].x = v;
}

// Function to animate object movement along x-axis
static void move_object_x(void *obj, int32_t from, int32_t to) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_time(&a, 200); // will be replaced with lv_anim_set_duration
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_set_values(&a, from, to);
    lv_anim_start(&a);
}

// Function to animate object size change
static void change_size_object(void *obj, int32_t from, int32_t to) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_time(&a, 200); // will be replaced with lv_anim_set_duration
    lv_anim_set_exec_cb(&a, anim_size_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_values(&a, from, to);
    lv_anim_start(&a);
}

// Function to set the status symbol based on the current state
static void set_status_symbol(lv_obj_t *widget, struct output_status_state state) {
    // Get child objects
    lv_obj_t *usb = lv_obj_get_child(widget, output_symbol_usb);
    lv_obj_t *usb_hid_status = lv_obj_get_child(widget, output_symbol_usb_hid_status);
    lv_obj_t *bt = lv_obj_get_child(widget, output_symbol_bt);
    lv_obj_t *bt_number = lv_obj_get_child(widget, output_symbol_bt_number);
    lv_obj_t *bt_status = lv_obj_get_child(widget, output_symbol_bt_status);

    // Set visibility and status based on the selected endpoint
    switch (state.selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        // Show USB symbols, hide BT symbols
        lv_obj_clear_flag(usb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(usb_hid_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bt_number, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bt_status, LV_OBJ_FLAG_HIDDEN);

        // Set USB HID status symbol
        if (state.usb_is_hid_ready) {
            lv_img_set_src(usb_hid_status, &sym_ok);
        } else {
            lv_img_set_src(usb_hid_status, &sym_nok);
        }
        break;
    case ZMK_TRANSPORT_BLE:
        // Show BT symbols, hide USB symbols
        lv_obj_add_flag(usb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(usb_hid_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bt_number, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bt_status, LV_OBJ_FLAG_HIDDEN);

        // Set BT profile number
        if (state.active_profile_index < (sizeof(sym_num) / sizeof(lv_img_dsc_t *))) {
            lv_img_set_src(bt_number, sym_num[state.active_profile_index]);
        } else {
            lv_img_set_src(bt_number, &sym_nok);
        }

        // Set BT status symbol
        if (state.active_profile_bonded) {
            if (state.active_profile_connected) {
                lv_img_set_src(bt_status, &sym_ok);
            } else {
                lv_img_set_src(bt_status, &sym_nok);
            }
        } else {
            lv_img_set_src(bt_status, &sym_open);
        }
        break;
    }
}
// Callback function to update output status
static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_output_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_status_symbol(widget->obj, state); }
}

// Define widget listener and subscriptions
ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);

// Initialize the output status widget
int zmk_widget_output_status_init(struct zmk_widget_output_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    // Set widget properties
    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(widget->obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(widget->obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widget->obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Create and configure child objects
    lv_obj_t *usb = lv_img_create(widget->obj);
    lv_img_set_src(usb, &sym_usb);

    lv_obj_t *usb_hid_status = lv_img_create(widget->obj);
    lv_obj_set_style_pad_left(usb_hid_status, 2, 0);

    lv_obj_t *bt = lv_img_create(widget->obj);
    lv_img_set_src(bt, &sym_bt);

    lv_obj_t *bt_number = lv_img_create(widget->obj);
    lv_obj_set_style_pad_left(bt_number, 2, 0);

    lv_obj_t *bt_status = lv_img_create(widget->obj);
    lv_obj_set_style_pad_left(bt_status, 2, 0);

    // Add widget to the list
    sys_slist_append(&widgets, &widget->node);

    // Initialize widget
    widget_output_status_init();
    return 0;
}

// Function to get the widget object
lv_obj_t *zmk_widget_output_status_obj(struct zmk_widget_output_status *widget) {
    return widget->obj;
}
