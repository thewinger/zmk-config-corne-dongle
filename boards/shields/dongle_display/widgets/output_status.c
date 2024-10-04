/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>

#include "output_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#define USB_LABEL "USB"
#define BT_LABEL  "BT"

enum output_symbol {
	output_symbol_usb,
	output_symbol_usb_hid_status,
	output_symbol_bt,
	output_symbol_selection_line
};

enum selection_line_state {
	selection_line_state_usb,
	selection_line_state_bt
} current_selection_line_state;

lv_point_t selection_line_points[] = {{-1, 0}, {12, 0}}; // will be replaced with lv_point_precise_t

struct output_status_state {
	struct zmk_endpoint_instance selected_endpoint;
	int active_profile_index;
	bool active_profile_connected;
	bool active_profile_bonded;
	bool usb_is_hid_ready;
};

static struct output_status_state get_state(const zmk_event_t *_eh)
{
	return (struct output_status_state){
		.selected_endpoint = zmk_endpoints_selected(),
		.active_profile_index = zmk_ble_active_profile_index(),
		.active_profile_connected = zmk_ble_active_profile_is_connected(),
		.active_profile_bonded = !zmk_ble_active_profile_is_open(),
		.usb_is_hid_ready = zmk_usb_is_hid_ready()};
}

static void anim_x_cb(void *var, int32_t v)
{
	lv_obj_set_x(var, v);
}

static void anim_size_cb(void *var, int32_t v)
{
	selection_line_points[1].x = v;
}

static void move_object_x(void *obj, int32_t from, int32_t to)
{
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, obj);
	lv_anim_set_time(&a, 200); // will be replaced with lv_anim_set_duration
	lv_anim_set_exec_cb(&a, anim_x_cb);
	lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
	lv_anim_set_values(&a, from, to);
	lv_anim_start(&a);
}

static void change_size_object(void *obj, int32_t from, int32_t to)
{
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, obj);
	lv_anim_set_time(&a, 200); // will be replaced with lv_anim_set_duration
	lv_anim_set_exec_cb(&a, anim_size_cb);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
	lv_anim_set_values(&a, from, to);
	lv_anim_start(&a);
}

static void set_status_symbol(lv_obj_t *widget, struct output_status_state state)
{
	lv_obj_t *usb = lv_obj_get_child(widget, output_symbol_usb);
	lv_obj_t *usb_hid_status = lv_obj_get_child(widget, output_symbol_usb_hid_status);
	lv_obj_t *bt = lv_obj_get_child(widget, output_symbol_bt);
	lv_obj_t *selection_line = lv_obj_get_child(widget, output_symbol_selection_line);

	switch (state.selected_endpoint.transport) {
	case ZMK_TRANSPORT_USB:
		if (current_selection_line_state != selection_line_state_usb) {
			move_object_x(selection_line, lv_obj_get_x(bt) - 1, lv_obj_get_x(usb) - 1);
			change_size_object(selection_line, 30, 30);
			current_selection_line_state = selection_line_state_usb;
		}
		break;
	case ZMK_TRANSPORT_BLE:
		if (current_selection_line_state != selection_line_state_bt) {
			move_object_x(selection_line, lv_obj_get_x(usb) - 1, lv_obj_get_x(bt) - 1);
			change_size_object(selection_line, 30, 30);
			current_selection_line_state = selection_line_state_bt;
		}
		break;
	}

	lv_label_set_text(usb, USB_LABEL);
	lv_label_set_text(bt, BT_LABEL);

	char bt_label[10];
	snprintf(bt_label, sizeof(bt_label), "%s%d", BT_LABEL, state.active_profile_index + 1);
	lv_label_set_text(bt, bt_label);

	if (state.usb_is_hid_ready) {
		lv_obj_clear_flag(usb_hid_status, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(usb_hid_status, LV_OBJ_FLAG_HIDDEN);
	}
}

static void output_status_update_cb(struct output_status_state state)
{
	struct zmk_widget_output_status *widget;
	SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
		set_status_symbol(widget->obj, state);
	}
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
			    output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);

int zmk_widget_output_status_init(struct zmk_widget_output_status *widget, lv_obj_t *parent)
{
	widget->obj = lv_obj_create(parent);

	lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

	lv_obj_t *usb = lv_label_create(widget->obj);
	lv_obj_align(usb, LV_ALIGN_TOP_LEFT, 1, 4);
	lv_label_set_text(usb, USB_LABEL);

	lv_obj_t *usb_hid_status = lv_obj_create(widget->obj);
	lv_obj_set_size(usb_hid_status, 5, 5);
	lv_obj_set_style_bg_color(usb_hid_status, lv_color_make(0, 255, 0), 0);
	lv_obj_align_to(usb_hid_status, usb, LV_ALIGN_OUT_RIGHT_MID, 2, 0);

	lv_obj_t *bt = lv_label_create(widget->obj);
	lv_obj_align_to(bt, usb, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
	lv_label_set_text(bt, BT_LABEL "1");

	static lv_style_t style_line;
	lv_style_init(&style_line);
	lv_style_set_line_width(&style_line, 2);

	lv_obj_t *selection_line;
	selection_line = lv_line_create(widget->obj);
	lv_line_set_points(selection_line, selection_line_points, 2);
	lv_obj_add_style(selection_line, &style_line, 0);
	lv_obj_align_to(selection_line, usb, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 1);

	sys_slist_append(&widgets, &widget->node);

	widget_output_status_init();
	return 0;
}

lv_obj_t *zmk_widget_output_status_obj(struct zmk_widget_output_status *widget)
{
	return widget->obj;
}
