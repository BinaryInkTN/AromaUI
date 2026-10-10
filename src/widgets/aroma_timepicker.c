#include "widgets/aroma_timepicker.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_time.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <math.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif








#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int tdp(int v)
{
#ifdef __ANDROID__
    return aroma_android_dp_to_px(v);
#else
    return v;
#endif
}

static inline float tdp_f(float v)
{
#ifdef __ANDROID__
    return aroma_android_dp_to_px_f(v);
#else
    return v;
#endif
}

#define TP_SLOP_DP 8
#define TP_HEADER_H_DP 120
#define TP_FOOTER_H_DP 52
#define TP_OK_W_DP 76
#define TP_CANCEL_W_DP 96
#define TP_AMPM_W_DP 64

typedef enum TpDialMode {
    TP_MODE_HOUR,
    TP_MODE_MINUTE
} TpDialMode;

struct AromaTimePicker {
    AromaRect rect;
    int hour;
    int minute;
    int p_hour;
    int p_minute;
    TpDialMode mode;
    bool use_24h;
    char title[48];
    AromaFont *font;
    bool use_theme_colors;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t dim_color;
    uint32_t accent_color;
    uint32_t accent_text;
    AromaTimePickerChangeCb on_change;
    void *user_data;
    int active_pointer_id;
    int down_x;
    int down_y;
    bool dial_armed;
    int pressed_foot;



    bool popup_open;
    bool drawing_popup;
    bool in_popup_event;
    AromaRect popup_rect;
};

static void tp_adjust(AromaEvent *e, int *x, int *y)
{
    *x = e->data.mouse.x;
    *y = e->data.mouse.y;
    if (e->event_type == EVENT_TYPE_TOUCH_DOWN ||
        e->event_type == EVENT_TYPE_TOUCH_UP ||
        e->event_type == EVENT_TYPE_TOUCH_MOVE) {
        *x = e->data.touch.x;
        *y = e->data.touch.y;
    }
    AromaNode *t = e->target_node;
    AromaTimePicker *self =
        t ? (AromaTimePicker *)t->node_widget_ptr : NULL;

    if (self && self->in_popup_event)
        return;
    AromaNode *cur = e->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int sx, sy;
            aroma_container_get_scroll(cur, &sx, &sy);
            *x += sx;
            *y += sy;
        }
        cur = cur->parent_node;
    }
}


static void tp_dial_geom(AromaTimePicker *t, int *cx, int *cy, int *r)
{
    int top = t->rect.y + tdp(TP_HEADER_H_DP);
    int bottom = t->rect.y + t->rect.height - tdp(TP_FOOTER_H_DP);
    int w = t->rect.width;
    int h = bottom - top;
    int rr = (w < h ? w : h) / 2 - tdp(20);
    if (rr < 24)
        rr = 24;
    *cx = t->rect.x + w / 2;
    *cy = top + h / 2;
    *r = rr;
}

typedef enum TpZone {
    TP_NONE,
    TP_HEAD_HOUR,
    TP_HEAD_MIN,
    TP_AM,
    TP_PM,
    TP_DIAL,
    TP_OK,
    TP_CANCEL
} TpZone;

static TpZone tp_zone(AromaTimePicker *t, int x, int y)
{
    if (x < t->rect.x || x >= t->rect.x + t->rect.width || y < t->rect.y ||
        y >= t->rect.y + t->rect.height)
        return TP_NONE;
    int header_h = tdp(TP_HEADER_H_DP);
    int footer_h = tdp(TP_FOOTER_H_DP);
    int ok_w = tdp(TP_OK_W_DP);
    int cancel_w = tdp(TP_CANCEL_W_DP);
    if (y >= t->rect.y + t->rect.height - footer_h) {
        if (x >= t->rect.x + t->rect.width - ok_w)
            return TP_OK;
        if (x >= t->rect.x + t->rect.width - ok_w - cancel_w)
            return TP_CANCEL;
        return TP_NONE;
    }
    if (y < t->rect.y + header_h) {
        int ampm_w = t->use_24h ? 0 : tdp(TP_AMPM_W_DP);
        int head_w = t->rect.width - ampm_w - 32;
        int hour_w = head_w / 2;
        int lx = x - (t->rect.x + 16);
        if (lx >= 0 && lx < hour_w)
            return TP_HEAD_HOUR;
        if (lx >= hour_w && lx < head_w)
            return TP_HEAD_MIN;
        if (!t->use_24h && x >= t->rect.x + t->rect.width - ampm_w - 8) {
            int mid = t->rect.y + header_h / 2;
            return y < mid ? TP_AM : TP_PM;
        }
        return TP_NONE;
    }
    int cx, cy, r;
    tp_dial_geom(t, &cx, &cy, &r);
    int dx = x - cx;
    int dy = y - cy;
    int grab = r + tdp(24);
    if (dx * dx + dy * dy <= grab * grab)
        return TP_DIAL;
    return TP_NONE;
}


static void tp_from_point(AromaTimePicker *t, int x, int y)
{
    int cx, cy, r;
    tp_dial_geom(t, &cx, &cy, &r);
    double dx = (double)(x - cx);
    double dy = (double)(y - cy);
    double ang = atan2(dx, -dy);
    double deg = ang * 180.0 / M_PI;
    if (deg < 0.0)
        deg += 360.0;
    if (t->mode == TP_MODE_HOUR) {
        if (t->use_24h) {
            double dist = sqrt(dx * dx + dy * dy);
            if (dist > (double)r * 0.78) {
                int idx = (int)((deg + 15.0) / 30.0) % 12;
                t->p_hour = (idx == 0) ? 0 : idx + 12;
            } else {
                int idx = (int)((deg + 15.0) / 30.0) % 12;
                t->p_hour = (idx == 0) ? 12 : idx;
            }
        } else {
            int idx = (int)((deg + 15.0) / 30.0) % 12;
            bool pm = t->p_hour >= 12;
            if (idx == 0)
                t->p_hour = pm ? 12 : 0;
            else
                t->p_hour = idx + (pm ? 12 : 0);
        }
    } else {
        t->p_minute = (int)((deg + 3.0) / 6.0) % 60;
    }
}


static void tp_sel_pos(AromaTimePicker *t, int cx, int cy, int r, int *sx,
                       int *sy)
{
    double deg;
    double rr = (double)r;
    if (t->mode == TP_MODE_HOUR) {
        if (t->use_24h) {
            if (t->p_hour == 0 || t->p_hour >= 13) {
                int v = t->p_hour % 12;
                deg = (double)v * 30.0;
            } else {
                deg = (double)(t->p_hour % 12) * 30.0;
                rr *= 0.55;
            }
        } else {
            deg = (double)(t->p_hour % 12) * 30.0;
        }
    } else {
        deg = (double)t->p_minute * 6.0;
    }
    double a = deg * M_PI / 180.0;
    *sx = cx + (int)(rr * sin(a));
    *sy = cy - (int)(rr * cos(a));
}

static void tp_fire(AromaNode *n, AromaTimePicker *t)
{
    if (t->on_change)
        t->on_change(n, t->hour, t->minute, t->user_data);
}


void aroma_timepicker_close_popup(AromaNode *n);

static void tp_apply_zone(AromaNode *node, AromaTimePicker *t, TpZone z)
{
    switch (z) {
    case TP_HEAD_HOUR:
        t->mode = TP_MODE_HOUR;
        break;
    case TP_HEAD_MIN:
        t->mode = TP_MODE_MINUTE;
        break;
    case TP_AM:
        if (t->p_hour >= 12)
            t->p_hour -= 12;
        break;
    case TP_PM:
        if (t->p_hour < 12)
            t->p_hour += 12;
        break;
    case TP_OK:
        t->hour = t->p_hour;
        t->minute = t->p_minute;
        tp_fire(node, t);
        aroma_timepicker_close_popup(node);
        break;
    case TP_CANCEL:
        t->p_hour = t->hour;
        t->p_minute = t->minute;
        aroma_timepicker_close_popup(node);
        break;
    default:
        break;
    }
}


static bool tp_handle_inner(AromaEvent *e, void *ud);

#define TP_POPUP_MAX 8
#define TP_PANEL_W_DP 340
#define TP_PANEL_H_DP 520

typedef struct {
    AromaNode *node;
    size_t window_id;
} TimePickerOverlayEntry;

static TimePickerOverlayEntry g_tp_overlays[TP_POPUP_MAX];
static size_t g_tp_overlay_count = 0;


void aroma_calendar_close_popups(void);
void aroma_datepicker_close_popups(void);

static void tp_overlay_register(AromaNode *node, size_t window_id)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_tp_overlay_count; i++) {
        if (g_tp_overlays[i].node == node) {
            g_tp_overlays[i].window_id = window_id;
            return;
        }
    }
    if (g_tp_overlay_count >= TP_POPUP_MAX)
        return;
    g_tp_overlays[g_tp_overlay_count].node = node;
    g_tp_overlays[g_tp_overlay_count].window_id = window_id;
    g_tp_overlay_count++;
}

static void tp_overlay_unregister(AromaNode *node)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_tp_overlay_count; i++) {
        if (g_tp_overlays[i].node == node) {
            g_tp_overlays[i] = g_tp_overlays[g_tp_overlay_count - 1];
            g_tp_overlay_count--;
            return;
        }
    }
}



static bool tp_field_screen(AromaNode *node, AromaTimePicker *t,
                            AromaRect *out)
{
    if (!node || !t || !out)
        return false;
    *out = t->rect;
    AromaNode *cur = node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int sx = 0, sy = 0;
            aroma_container_get_scroll(cur, &sx, &sy);
            out->x -= sx;
            out->y -= sy;
            AromaRect *viewport = aroma_node_get_rect(cur);
            if (viewport &&
                (out->x + out->width <= viewport->x ||
                 out->x >= viewport->x + viewport->width ||
                 out->y + out->height <= viewport->y ||
                 out->y >= viewport->y + viewport->height))
                return false;
        }
        cur = cur->parent_node;
    }
    return true;
}



static bool tp_compute_popup(AromaNode *node, AromaTimePicker *t)
{
    AromaRect field;
    if (!tp_field_screen(node, t, &field))
        return false;
    int pw = tdp(TP_PANEL_W_DP);
    int ph = tdp(TP_PANEL_H_DP);
    if (pw <= 0)
        pw = 1;
    if (ph <= 0)
        ph = 1;
    int win_w = 0, win_h = 0;
    AromaPlatformInterface *platform =
        aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
        platform->get_window_size(0, &win_w, &win_h);
    if (win_w > 0 && win_h > 0) {
        int margin = tdp(16);
        if (pw > win_w - 2 * margin)
            pw = win_w - 2 * margin;
        if (ph > win_h - 2 * margin)
            ph = win_h - 2 * margin;
        if (pw < 1)
            pw = 1;
        if (ph < 1)
            ph = 1;
        t->popup_rect.x = (win_w - pw) / 2;
        t->popup_rect.y = (win_h - ph) / 2;
        if (t->popup_rect.x < 0)
            t->popup_rect.x = 0;
        if (t->popup_rect.y < 0)
            t->popup_rect.y = 0;
        t->popup_rect.width = pw;
        t->popup_rect.height = ph;
        return true;
    }
    int px = field.x;
    int py = field.y + field.height + tdp(4);
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    t->popup_rect.x = px;
    t->popup_rect.y = py;
    t->popup_rect.width = pw;
    t->popup_rect.height = ph;
    return true;
}

void aroma_timepicker_close_popup(AromaNode *n);

void aroma_timepicker_open_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    if (t->popup_open)
        return;
    aroma_calendar_close_popups();
    aroma_datepicker_close_popups();
    if (!tp_compute_popup(n, t))
        return;
    t->popup_open = true;
    t->p_hour = t->hour;
    t->p_minute = t->minute;
    tp_overlay_register(n, 0);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_timepicker_close_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    if (!t->popup_open)
        return;
    t->popup_open = false;
    t->p_hour = t->hour;
    t->p_minute = t->minute;
    tp_overlay_unregister(n);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_timepicker_close_popups(void)
{
    while (g_tp_overlay_count > 0) {
        AromaNode *node = g_tp_overlays[0].node;
        if (!node) {
            g_tp_overlays[0] = g_tp_overlays[g_tp_overlay_count - 1];
            g_tp_overlay_count--;
            continue;
        }
        aroma_timepicker_close_popup(node);
    }
}




static bool tp_handle(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;
    AromaTimePicker *t = (AromaTimePicker *)e->target_node->node_widget_ptr;
    if (!t)
        return false;
    if (!t->popup_open) {
        switch (e->event_type) {
        case EVENT_TYPE_MOUSE_MOVE:
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_TOUCH_DOWN:
        case EVENT_TYPE_MOUSE_CLICK: {
            int x, y;
            tp_adjust(e, &x, &y);
            if (x < t->rect.x || x >= t->rect.x + t->rect.width ||
                y < t->rect.y || y >= t->rect.y + t->rect.height)
                return false;
            aroma_timepicker_open_popup(e->target_node);
            return true;
        }
        default:
            return false;
        }
    }

    int x = e->data.mouse.x;
    int y = e->data.mouse.y;
    if (e->event_type == EVENT_TYPE_TOUCH_DOWN ||
        e->event_type == EVENT_TYPE_TOUCH_UP ||
        e->event_type == EVENT_TYPE_TOUCH_MOVE) {
        x = e->data.touch.x;
        y = e->data.touch.y;
    }
    bool in_popup = x >= t->popup_rect.x &&
                    x < t->popup_rect.x + t->popup_rect.width &&
                    y >= t->popup_rect.y &&
                    y < t->popup_rect.y + t->popup_rect.height;
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_MOUSE_EXIT:
        if (!in_popup)
            return false;
        break;
    case EVENT_TYPE_TOUCH_DOWN:
    case EVENT_TYPE_MOUSE_CLICK:
        if (!in_popup) {
            aroma_timepicker_close_popup(e->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_TOUCH_UP:
    case EVENT_TYPE_TOUCH_MOVE:
        break;
    default:
        return false;
    }
    AromaRect saved = t->rect;
    t->rect = t->popup_rect;
    t->in_popup_event = true;
    bool r = tp_handle_inner(e, ud);
    t->in_popup_event = false;
    t->rect = saved;
    return r;
}

static bool tp_handle_inner(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;
    AromaTimePicker *t = (AromaTimePicker *)e->target_node->node_widget_ptr;
    if (!t)
        return false;
    int x, y;
    tp_adjust(e, &x, &y);
    bool in = x >= t->rect.x && x < t->rect.x + t->rect.width &&
              y >= t->rect.y && y < t->rect.y + t->rect.height;
    void (*rd)(void *) = (void (*)(void *))ud;
    int slop = tdp(TP_SLOP_DP);
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
        return in;
    case EVENT_TYPE_MOUSE_EXIT:
        return false;
    case EVENT_TYPE_TOUCH_DOWN:
        if (!in || t->active_pointer_id != -1)
            return false;
        t->active_pointer_id = e->data.touch.id;
        t->down_x = x;
        t->down_y = y;
        t->dial_armed = (tp_zone(t, x, y) == TP_DIAL);
        {
            TpZone z = tp_zone(t, x, y);
            t->pressed_foot = (z == TP_OK) ? 1 : ((z == TP_CANCEL) ? 0 : -1);
            if (t->pressed_foot != -1)
                aroma_node_invalidate(e->target_node);
        }
        if (t->dial_armed) {
            tp_from_point(t, x, y);
            aroma_node_invalidate(e->target_node);
        }
        return true;
    case EVENT_TYPE_TOUCH_MOVE:
        if (t->active_pointer_id == -1 ||
            t->active_pointer_id != e->data.touch.id)
            return t->active_pointer_id != -1;
        if (t->dial_armed) {
            tp_from_point(t, x, y);
            aroma_node_invalidate(e->target_node);
        } else if (t->pressed_foot != -1) {
            int dx = x - t->down_x;
            int dy = y - t->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx >= slop || dy >= slop) {
                t->pressed_foot = -1;
                aroma_node_invalidate(e->target_node);
            }
        }
        return true;
    case EVENT_TYPE_TOUCH_UP: {
        if (t->active_pointer_id != e->data.touch.id)
            return false;
        t->active_pointer_id = -1;
        bool was_dial = t->dial_armed;
        int armed_foot = t->pressed_foot;
        t->dial_armed = false;
        t->pressed_foot = -1;
        aroma_node_invalidate(e->target_node);
        if (!in)
            return true;
        TpZone z = tp_zone(t, x, y);
        if (was_dial) {
            int dx = x - t->down_x;
            int dy = y - t->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx < slop && dy < slop && z == TP_DIAL)
                tp_from_point(t, x, y);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z == TP_NONE || z == TP_DIAL)
            return true;
        {
            int dx = x - t->down_x;
            int dy = y - t->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx >= slop || dy >= slop)
                return true;
        }
        if ((z == TP_OK && armed_foot != 1) ||
            (z == TP_CANCEL && armed_foot != 0))
            return true;
        tp_apply_zone(e->target_node, t, z);
        aroma_node_invalidate(e->target_node);
        if (rd)
            rd(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_CLICK: {
        if (t->active_pointer_id != -1 || !in)
            break;
        TpZone z = tp_zone(t, x, y);
        if (z == TP_NONE)
            break;
        if (z == TP_DIAL) {
            tp_from_point(t, x, y);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        tp_apply_zone(e->target_node, t, z);
        aroma_node_invalidate(e->target_node);
        if (rd)
            rd(NULL);
        return true;
    }
    default:
        break;
    }
    return false;
}

AromaNode *aroma_timepicker_create(AromaNode *parent, int x, int y, int width,
                                   int height, int hour, int minute)
{
    if (!parent)
        return NULL;
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    AromaTimePicker *t = (AromaTimePicker *)aroma_widget_alloc(sizeof(*t));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->rect.x = x;
    t->rect.y = y;
    t->rect.width = width > 0 ? width : 300;
    t->rect.height = height > 0 ? height : 120;
    t->hour = (hour >= 0 && hour < 24) ? hour : 12;
    t->minute = (minute >= 0 && minute < 60) ? minute : 0;
    t->p_hour = t->hour;
    t->p_minute = t->minute;
    t->mode = TP_MODE_HOUR;
    t->use_24h = false;
    strncpy(t->title, "Select time", sizeof(t->title) - 1);
    t->use_theme_colors = true;
    t->active_pointer_id = -1;
    t->pressed_foot = -1;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, t);
    if (!node) {
        aroma_widget_free(t);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_timepicker_draw);
    aroma_node_invalidate(node);
    return node;
}

void aroma_timepicker_set_time(AromaNode *n, int hour, int minute)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    if (hour >= 0 && hour < 24)
        t->hour = hour;
    if (minute >= 0 && minute < 60)
        t->minute = minute;
    t->p_hour = t->hour;
    t->p_minute = t->minute;
    aroma_node_invalidate(n);
}

void aroma_timepicker_get_time(AromaNode *n, int *h, int *m)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    if (h)
        *h = t->hour;
    if (m)
        *m = t->minute;
}

void aroma_timepicker_confirm(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    t->hour = t->p_hour;
    t->minute = t->p_minute;
    tp_fire(n, t);
    aroma_node_invalidate(n);
}

void aroma_timepicker_cancel(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    t->p_hour = t->hour;
    t->p_minute = t->minute;
    aroma_node_invalidate(n);
}

void aroma_timepicker_set_title(AromaNode *n, const char *title)
{
    if (!n || !n->node_widget_ptr || !title)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    strncpy(t->title, title, sizeof(t->title) - 1);
    t->title[sizeof(t->title) - 1] = '\0';
    aroma_node_invalidate(n);
}

void aroma_timepicker_set_24h(AromaNode *n, bool use_24h)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaTimePicker *)n->node_widget_ptr)->use_24h = use_24h;
    aroma_node_invalidate(n);
}

void aroma_timepicker_set_on_change(AromaNode *n, AromaTimePickerChangeCb cb,
                                    void *ud)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaTimePicker *t = (AromaTimePicker *)n->node_widget_ptr;
    t->on_change = cb;
    t->user_data = ud;
}

void aroma_timepicker_set_font(AromaNode *n, AromaFont *font)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaTimePicker *)n->node_widget_ptr)->font = font;
    aroma_node_invalidate(n);
}

bool aroma_timepicker_setup_events(AromaNode *n, void (*on_redraw)(void *),
                                   void *ud)
{
    (void)ud;
    if (!n)
        return false;
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_MOVE, tp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_EXIT, tp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_DOWN, tp_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_UP, tp_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_MOVE, tp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_CLICK, tp_handle,
                          (void *)on_redraw, 90);
    return true;
}


static void tp_field_text(AromaTimePicker *t, char *out, size_t n)
{
    if (t->use_24h) {
        snprintf(out, n, "%02d:%02d", t->hour, t->minute);
    } else {
        int h = t->hour % 12 == 0 ? 12 : t->hour % 12;
        snprintf(out, n, "%02d:%02d %s", h, t->minute,
                 t->hour >= 12 ? "PM" : "AM");
    }
}


static void tp_draw_field(AromaNode *node, AromaTimePicker *t,
                          size_t window_id, AromaGraphicsInterface *gfx)
{
    int pad = tdp(16);
    int glyph = tdp(24);
    char text[16];
    tp_field_text(t, text, sizeof(text));
    gfx->render_text(window_id, t->font, text, t->rect.x + pad,
                     t->rect.y + (t->rect.height -
                                  aroma_font_get_line_height(t->font)) /
                                     2,
                     t->text_color, 1.0f);

    int gx = t->rect.x + t->rect.width - pad - glyph;
    int gy = t->rect.y + (t->rect.height - glyph) / 2;
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, gx, gy, glyph, glyph,
                                   t->dim_color, tdp(1) > 0 ? tdp(1) : 1,
                                   true, (float)glyph / 2.0f);
    int cx = gx + glyph / 2;
    int cy = gy + glyph / 2;
    float hw = tdp_f(2.0f);
    if (hw <= 0.0f)
        hw = 1.0f;
    if (gfx->draw_line) {
        gfx->draw_line(window_id, cx, cy, cx, cy - glyph / 4,
                       t->dim_color, hw, true);
        gfx->draw_line(window_id, cx, cy, cx + glyph / 4, cy,
                       t->dim_color, hw, true);
    }
    (void)node;
}

void aroma_timepicker_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    AromaTimePicker *t = (AromaTimePicker *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text)
        return;
    if (!t->font) {
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i].is_active &&
                g_windows[i].window_id == window_id &&
                g_windows[i].default_font) {
                t->font = g_windows[i].default_font;
                break;
            }
        }
    }
    if (t->use_theme_colors) {
        AromaTheme th = aroma_theme_get_global();
        t->bg_color = th.colors.surface;
        t->border_color = th.colors.border;
        t->text_color = th.colors.text_primary;
        t->dim_color = th.colors.text_secondary;
        t->accent_color = th.colors.primary;
        t->accent_text = th.colors.surface;
    }
    gfx->fill_rectangle(window_id, t->rect.x, t->rect.y, t->rect.width,
                        t->rect.height, t->bg_color, true, tdp_f(12.0f));
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, t->rect.x, t->rect.y,
                                    t->rect.width, t->rect.height,
                                    t->border_color, tdp(1) > 0 ? tdp(1) : 1,
                                    true, tdp_f(12.0f));
    if (!t->font)
        return;
    if (!t->drawing_popup) {
        tp_draw_field(node, t, window_id, gfx);
        return;
    }
    int header_h = tdp(TP_HEADER_H_DP);
    int footer_h = tdp(TP_FOOTER_H_DP);
    int pad = tdp(16);
    gfx->render_text(window_id, t->font, t->title, t->rect.x + pad,
                     t->rect.y + tdp(12), t->dim_color, 0.9f);
    int disp_h = t->use_24h ? t->p_hour
                            : (t->p_hour % 12 == 0 ? 12 : t->p_hour % 12);
    char hb[4], mb[4], colon[] = ":";
    snprintf(hb, sizeof(hb), "%02d", disp_h);
    snprintf(mb, sizeof(mb), "%02d", t->p_minute);
    int hbx = t->rect.x + pad;
    int hby = t->rect.y + tdp(36);
    uint32_t hcol = t->mode == TP_MODE_HOUR ? t->text_color : t->dim_color;
    uint32_t mcol = t->mode == TP_MODE_MINUTE ? t->text_color : t->dim_color;
    gfx->render_text(window_id, t->font, hb, hbx, hby, hcol, 2.0f);
    int hw = aroma_font_get_line_width(t->font, hb) * 2;
    int hw_min = tdp(40);
    if (hw < hw_min)
        hw = hw_min;
    gfx->render_text(window_id, t->font, colon, hbx + hw + tdp(4), hby,
                     t->dim_color, 2.0f);
    gfx->render_text(window_id, t->font, mb, hbx + hw + tdp(24), hby, mcol,
                     2.0f);
    if (!t->use_24h) {
        int ampm_w = tdp(TP_AMPM_W_DP);
        int ax = t->rect.x + t->rect.width - ampm_w - tdp(12);
        int ah = (header_h - tdp(24)) / 2;
        if (ah < tdp(24))
            ah = tdp(24);
        int ay = t->rect.y + tdp(12);
        bool is_pm = t->p_hour >= 12;
        int pill_r = ah / 2;
        gfx->fill_rectangle(window_id, ax, ay, ampm_w, ah,
                            is_pm ? t->bg_color : t->accent_color, true,
                            (float)pill_r);
        if (!is_pm && gfx->draw_hollow_rectangle)
            gfx->draw_hollow_rectangle(window_id, ax, ay, ampm_w, ah,
                                       t->accent_color,
                                       tdp(1) > 0 ? tdp(1) : 1, true,
                                       (float)pill_r);
        gfx->fill_rectangle(window_id, ax, ay + ah, ampm_w, ah,
                            is_pm ? t->accent_color : t->bg_color, true,
                            (float)pill_r);
        if (is_pm && gfx->draw_hollow_rectangle)
            gfx->draw_hollow_rectangle(window_id, ax, ay + ah, ampm_w, ah,
                                       t->accent_color,
                                       tdp(1) > 0 ? tdp(1) : 1, true,
                                       (float)pill_r);
        int amw = aroma_font_get_line_width(t->font, "AM");
        int amh = aroma_font_get_line_height(t->font);
        gfx->render_text(window_id, t->font, "AM", ax + (ampm_w - amw) / 2,
                         ay + (ah - amh) / 2,
                         is_pm ? t->accent_color : t->accent_text, 0.9f);
        gfx->render_text(window_id, t->font, "PM", ax + (ampm_w - amw) / 2,
                         ay + ah + (ah - amh) / 2,
                         is_pm ? t->accent_text : t->accent_color, 0.9f);
    }
    if (gfx->draw_line)
        gfx->draw_line(window_id, t->rect.x + pad, t->rect.y + header_h - 1,
                       t->rect.x + t->rect.width - pad,
                       t->rect.y + header_h - 1, t->border_color,
                       tdp_f(1.0f) > 0.0f ? tdp_f(1.0f) : 1.0f, false);

    int cx, cy, r;
    tp_dial_geom(t, &cx, &cy, &r);
    uint32_t dial_bg = aroma_color_blend(t->bg_color, t->text_color, 0.05f);
    gfx->fill_rectangle(window_id, cx - r, cy - r, 2 * r, 2 * r, dial_bg,
                        true, (float)r);



    int ring_w = tdp(2);
    if (ring_w < 1)
        ring_w = 1;
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, cx - r, cy - r, 2 * r, 2 * r,
                                   t->border_color, ring_w, true,
                                   (float)r);
    int sx, sy;
    tp_sel_pos(t, cx, cy, r, &sx, &sy);
    int sel_r = tdp(14);
    if (sel_r < 8)
        sel_r = 8;
    if (gfx->draw_line)
        gfx->draw_line(window_id, cx, cy, sx, sy, t->accent_color,
                       tdp_f(2.0f) > 0.0f ? tdp_f(2.0f) : 1.0f, true);
    gfx->fill_rectangle(window_id, sx - sel_r, sy - sel_r, 2 * sel_r,
                        2 * sel_r, t->accent_color, true, (float)sel_r);
    int hub = tdp(4);
    if (hub < 2)
        hub = 2;
    gfx->fill_rectangle(window_id, cx - hub, cy - hub, 2 * hub, 2 * hub,
                        t->accent_color, true, (float)hub);
    for (int i = 0; i < 12; i++) {
        double a = (double)i * 30.0 * M_PI / 180.0;
        int nx = cx + (int)((double)r * sin(a));
        int ny = cy - (int)((double)r * cos(a));
        char num[4];
        if (t->use_24h) {
            if (i == 0)
                snprintf(num, sizeof(num), "00");
            else
                snprintf(num, sizeof(num), "%02d", i + 12);
        } else {
            snprintf(num, sizeof(num), "%d", i == 0 ? 12 : i);
        }
        int nw = aroma_font_get_line_width(t->font, num);
        int nh = aroma_font_get_line_height(t->font);
        bool is_sel;
        if (t->mode == TP_MODE_HOUR) {
            if (t->use_24h)
                is_sel = (i == 0 && t->p_hour == 0) ||
                         (i != 0 && t->p_hour == i + 12);
            else
                is_sel = (t->p_hour % 12) == i;
        } else {
            is_sel = (t->p_minute / 5) % 12 == i && t->p_minute % 5 == 0;
        }
        gfx->render_text(window_id, t->font, num, nx - nw / 2, ny - nh / 2,
                         is_sel ? t->accent_text : t->text_color, 0.95f);
    }
    if (t->use_24h) {
        for (int i = 0; i < 12; i++) {
            double a = (double)i * 30.0 * M_PI / 180.0;
            int nx = cx + (int)((double)r * 0.55 * sin(a));
            int ny = cy - (int)((double)r * 0.55 * cos(a));
            char num[4];
            snprintf(num, sizeof(num), "%d", i == 0 ? 12 : i);
            int nw = aroma_font_get_line_width(t->font, num);
            int nh = aroma_font_get_line_height(t->font);
            bool is_sel = t->mode == TP_MODE_HOUR &&
                          ((i == 0 && t->p_hour == 12) ||
                           (i != 0 && t->p_hour == i));
            gfx->render_text(window_id, t->font, num, nx - nw / 2,
                             ny - nh / 2,
                             is_sel ? t->accent_text : t->dim_color, 0.8f);
        }
    } else if (t->mode == TP_MODE_MINUTE) {
        int tick = tdp(2);
        if (tick < 1)
            tick = 1;
        for (int m = 0; m < 60; m += 5) {
            if (m % 15 == 0)
                continue;
            double a = (double)m * 6.0 * M_PI / 180.0;
            int nx = cx + (int)((double)r * sin(a));
            int ny = cy - (int)((double)r * cos(a));
            gfx->fill_rectangle(window_id, nx - tick, ny - tick, 2 * tick,
                                2 * tick,
                                m == t->p_minute ? t->accent_color
                                                 : t->dim_color,
                                true, (float)tick);
        }
    }

    int fy = t->rect.y + t->rect.height - footer_h;
    int ok_w = tdp(TP_OK_W_DP);
    int cancel_w = tdp(TP_CANCEL_W_DP);
    int lh = aroma_font_get_line_height(t->font);
    int chw = aroma_font_get_line_width(t->font, "Cancel");
    int ohw = aroma_font_get_line_width(t->font, "OK");
    int pill_pad = tdp(6);
    if (t->pressed_foot == 0 || t->pressed_foot == 1) {
        uint32_t shade = aroma_color_blend(t->accent_color, t->bg_color,
                                           0.22f);
        int zx = t->rect.x + t->rect.width - ok_w - cancel_w +
                 (t->pressed_foot == 0 ? 0 : cancel_w);
        int zw = t->pressed_foot == 0 ? cancel_w : ok_w;
        gfx->fill_rectangle(window_id, zx + pill_pad / 2,
                            fy + pill_pad / 2, zw - pill_pad,
                            footer_h - pill_pad, shade, true, tdp_f(8.0f));
    }
    gfx->render_text(window_id, t->font, "Cancel",
                     t->rect.x + t->rect.width - ok_w - cancel_w +
                         (cancel_w - chw) / 2,
                     fy + (footer_h - lh) / 2, t->accent_color, 1.0f);
    gfx->render_text(window_id, t->font, "OK",
                     t->rect.x + t->rect.width - ok_w + (ok_w - ohw) / 2,
                     fy + (footer_h - lh) / 2, t->accent_color, 1.0f);
}

void aroma_timepicker_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr) {
        AromaTimePicker *t = (AromaTimePicker *)node->node_widget_ptr;
        if (t->popup_open) {
            t->popup_open = false;
            tp_overlay_unregister(node);
        }
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}

bool aroma_timepicker_is_popup_open(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    return ((AromaTimePicker *)n->node_widget_ptr)->popup_open;
}

bool aroma_timepicker_any_popup_open(void)
{
    for (size_t i = 0; i < g_tp_overlay_count; i++) {
        AromaNode *node = g_tp_overlays[i].node;
        if (!node || !node->node_widget_ptr)
            continue;
        if (((AromaTimePicker *)node->node_widget_ptr)->popup_open)
            return true;
    }
    return false;
}

static bool tp_node_visible(AromaNode *node)
{
    AromaNode *anc = node;
    int depth = 0;
    while (anc && depth < 64) {
        if (anc->is_hidden)
            return false;
        anc = anc->parent_node;
        depth++;
    }
    return true;
}

void aroma_timepicker_render_overlays(size_t window_id)
{
    if (g_tp_overlay_count == 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    for (size_t i = 0; i < g_tp_overlay_count;) {
        AromaNode *node = g_tp_overlays[i].node;
        if (!node || !node->node_widget_ptr || !tp_node_visible(node)) {
            if (node)
                tp_overlay_unregister(node);
            else {
                g_tp_overlays[i] = g_tp_overlays[g_tp_overlay_count - 1];
                g_tp_overlay_count--;
            }
            continue;
        }
        AromaTimePicker *t = (AromaTimePicker *)node->node_widget_ptr;
        if (!t->popup_open) {
            tp_overlay_unregister(node);
            continue;
        }
        if (!tp_compute_popup(node, t)) {
            aroma_timepicker_close_popup(node);
            continue;
        }
        int win_w = 0, win_h = 0;
        AromaPlatformInterface *platform =
            aroma_backend_abi.get_platform_interface();
        if (platform && platform->get_window_size)
            platform->get_window_size(window_id, &win_w, &win_h);
        if (win_w > 0 && win_h > 0 && gfx->fill_rectangle)
            gfx->fill_rectangle(window_id, 0, 0, win_w, win_h, 0x80000000,
                                false, 0.0f);
        AromaRect saved = t->rect;
        t->rect = t->popup_rect;
        t->drawing_popup = true;
        aroma_timepicker_draw(node, window_id);
        t->drawing_popup = false;
        t->rect = saved;
        i++;
    }
}

bool aroma_timepicker_overlay_hit_test(int x, int y, AromaNode **out_node)
{
    for (size_t i = 0; i < g_tp_overlay_count; i++) {
        AromaNode *node = g_tp_overlays[i].node;
        if (!node || !node->node_widget_ptr || !tp_node_visible(node))
            continue;
        AromaTimePicker *t = (AromaTimePicker *)node->node_widget_ptr;
        if (!t->popup_open)
            continue;


        (void)x;
        (void)y;
        if (out_node)
            *out_node = node;
        return true;
    }
    return false;
}
