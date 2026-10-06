/*
 * Layer relay display for the peripheral (right) half.
 *
 * The central sends "layer index + 1" through zmk-split-peripheral-output-relay.
 * This file:
 *   1. defines a small output device that receives that value, and
 *   2. provides the nice!view status screen for the peripheral:
 *      battery + connection on top, active layer name below.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_layer_relay_display

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include <zmk/output/output_generic_api.h>

#include "util.h"

/* ---------- layer names from devicetree ---------- */

#define LAYER_NAME_ENTRY(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx),
static const char *const layer_names[] = {
    DT_FOREACH_PROP_ELEM(DT_DRV_INST(0), layer_names, LAYER_NAME_ENTRY)};
#define LAYER_NAME_COUNT ARRAY_SIZE(layer_names)

/* ---------- screen state ---------- */

static lv_color_t top_buf[CANVAS_SIZE * CANVAS_SIZE];
static lv_color_t layer_buf[CANVAS_SIZE * CANVAS_SIZE];
static lv_obj_t *top_canvas;
static lv_obj_t *layer_canvas;

static struct status_state state;
static uint8_t current_layer;

static void draw_top(void) {
    if (top_canvas == NULL) {
        return;
    }

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(top_canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_dsc);
    draw_battery(top_canvas, &state);
    lv_canvas_draw_text(top_canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(top_canvas, top_buf);
}

static void draw_layer(void) {
    if (layer_canvas == NULL) {
        return;
    }

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    lv_draw_rect_dsc_t rect_dsc;
    init_rect_dsc(&rect_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(layer_canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_dsc);

    char fallback[12];
    const char *text;
    if (current_layer < LAYER_NAME_COUNT) {
        text = layer_names[current_layer];
    } else {
        snprintf(fallback, sizeof(fallback), "LAYER %u", current_layer);
        text = fallback;
    }
    lv_canvas_draw_text(layer_canvas, 0, 24, CANVAS_SIZE, &label_dsc, text);

    rotate_canvas(layer_canvas, layer_buf);
}

/* ---------- battery + connection (same as stock nice!view peripheral) ---------- */

static void battery_update_cb(struct battery_status_state bs) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    state.charging = bs.usb_present;
#endif
    state.battery = bs.level;
    draw_top();
}

static struct battery_status_state battery_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(lrd_battery, struct battery_status_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(lrd_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(lrd_battery, zmk_usb_conn_state_changed);
#endif

struct conn_state {
    bool connected;
};

static void conn_update_cb(struct conn_state cs) {
    state.connected = cs.connected;
    draw_top();
}

static struct conn_state conn_get_state(const zmk_event_t *eh) {
    return (struct conn_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(lrd_conn, struct conn_state, conn_update_cb, conn_get_state)
ZMK_SUBSCRIPTION(lrd_conn, zmk_split_peripheral_status_changed);

/* ---------- layer value from the relay ---------- */

static atomic_t pending_layer = ATOMIC_INIT(0);

static void layer_redraw_work_cb(struct k_work *work) {
    current_layer = (uint8_t)atomic_get(&pending_layer);
    draw_layer();
}

static K_WORK_DEFINE(layer_redraw_work, layer_redraw_work_cb);

static int lrd_set_value(const struct device *dev, uint8_t value) {
    if (value == 0) {
        /* 0 means "nothing"; real layers arrive as index + 1 */
        return 0;
    }
    atomic_set(&pending_layer, value - 1);
    LOG_DBG("relayed layer %d", value - 1);
    k_work_submit_to_queue(zmk_display_work_q(), &layer_redraw_work);
    return 0;
}

static int lrd_get_ready(const struct device *dev) { return 1; }

static const struct output_generic_api lrd_api = {
    .set_value = lrd_set_value,
    .get_ready = lrd_get_ready,
};

static int lrd_init(const struct device *dev) { return 0; }

DEVICE_DT_INST_DEFINE(0, lrd_init, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &lrd_api);

/* ---------- the status screen ---------- */

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_set_size(box, 160, 68);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 0, 0);

    top_canvas = lv_canvas_create(box);
    lv_obj_align(top_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top_canvas, top_buf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    layer_canvas = lv_canvas_create(box);
    lv_obj_align(layer_canvas, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(layer_canvas, layer_buf, CANVAS_SIZE, CANVAS_SIZE,
                         LV_IMG_CF_TRUE_COLOR);

    lrd_battery_init();
    lrd_conn_init();
    draw_layer();

    return screen;
}
