#include "widgets/aroma_calendar.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_time.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <time.h>
#ifdef __ANDROID__
#include "aroma_android.h"
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

#define CAL_TOUCH_SLOP_DP 8
#define CAL_MIN_CELL_DP 48
#define CAL_HEADER_H_DP 56
#define CAL_WEEK_H_DP 32

struct AromaCalendar {
    AromaRect rect;
    int year;
    int month;
    int selected_day;
    bool has_selection;
    int pressed_day;
    int pressed_month_off;
    int pressed_nav;
    bool pressed_title;
    bool show_years;
    int year_page;
    bool popup_open;
    bool drawing_popup;
    bool in_popup_event;
    AromaRect popup_rect;
    AromaFont *font;
    bool use_theme_colors;
    uint32_t bg_color;
    uint32_t header_color;
    uint32_t text_color;
    uint32_t dim_color;
    uint32_t accent_color;
    uint32_t accent_text;
    AromaCalendarSelectCb on_select;
    void *user_data;
    int active_pointer_id;
    int down_x;
    int down_y;
};

bool aroma_calendar_is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int aroma_calendar_days_in_month(int year, int month)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 30;
    if (month == 2 && aroma_calendar_is_leap(year))
        return 29;
    return dm[month - 1];
}

int aroma_calendar_first_weekday(int year, int month)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    mktime(&t);
    return t.tm_wday;
}


void aroma_calendar_close_popup(AromaNode *n);
void aroma_datepicker_close_popups(void);
void aroma_timepicker_close_popups(void);

static void cal_today(int *y, int *m, int *d)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        *y = t->tm_year + 1900;
        *m = t->tm_mon + 1;
        *d = t->tm_mday;
    } else {
        *y = 2026;
        *m = 10;
        *d = 6;
    }
}

static void cal_adjust(AromaEvent *event, int *x, int *y)
{
    *x = event->data.mouse.x;
    *y = event->data.mouse.y;
    if (event->event_type == EVENT_TYPE_TOUCH_DOWN ||
        event->event_type == EVENT_TYPE_TOUCH_UP ||
        event->event_type == EVENT_TYPE_TOUCH_MOVE) {
        *x = event->data.touch.x;
        *y = event->data.touch.y;
    }
    AromaNode *t = event->target_node;
    AromaCalendar *self =
        t ? (AromaCalendar *)t->node_widget_ptr : NULL;

    if (self && self->in_popup_event)
        return;
    AromaNode *cur = t ? t->parent_node : NULL;
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

static bool cal_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}


static void cal_geom(AromaCalendar *c, int *gx, int *gy, int *cw, int *ch,
                     int *header_h)
{
    *header_h = tdp(CAL_HEADER_H_DP);
    int week_h = tdp(CAL_WEEK_H_DP);
    *cw = c->rect.width / 7;
    if (*cw < 1)
        *cw = 1;
    *gx = c->rect.x + (c->rect.width - (*cw) * 7) / 2;
    *gy = c->rect.y + *header_h + week_h;
    int avail = c->rect.y + c->rect.height - *gy;
    *ch = avail / 6;
    if (*ch < 1)
        *ch = 1;
}

static void cal_month_year(const AromaCalendar *c, int off, int *y, int *m)
{
    *y = c->year;
    *m = c->month + off;
    while (*m < 1) {
        *m += 12;
        (*y)--;
    }
    while (*m > 12) {
        *m -= 12;
        (*y)++;
    }
}



static int cal_pick(AromaCalendar *c, int x, int y, int *month_off,
                    bool *is_nav_prev, bool *is_nav_next, bool *is_title)
{
    *month_off = 0;
    *is_nav_prev = false;
    *is_nav_next = false;
    *is_title = false;
    int gx, gy, cw, ch, header_h;
    cal_geom(c, &gx, &gy, &cw, &ch, &header_h);
    int nav = tdp(CAL_MIN_CELL_DP);
    if (y >= c->rect.y && y < c->rect.y + header_h) {
        if (x >= c->rect.x + c->rect.width - 2 * nav &&
            x < c->rect.x + c->rect.width - nav) {
            *is_nav_prev = true;
            return -2;
        }
        if (x >= c->rect.x + c->rect.width - nav &&
            x < c->rect.x + c->rect.width) {
            *is_nav_next = true;
            return -2;
        }
        if (x >= c->rect.x &&
            x < c->rect.x + c->rect.width - 2 * nav) {
            *is_title = true;
            return -3;
        }
        return -1;
    }
    if (!cal_in(x, y, gx, gy, cw * 7, ch * 6))
        return -1;
    int col = (x - gx) / cw;
    int row = (y - gy) / ch;
    if (col < 0)
        col = 0;
    if (col > 6)
        col = 6;
    if (row < 0)
        row = 0;
    if (row > 5)
        row = 5;
    int first = aroma_calendar_first_weekday(c->year, c->month);
    int cell = row * 7 + col;
    int dim = aroma_calendar_days_in_month(c->year, c->month);
    if (cell < first) {
        int py, pm;
        cal_month_year(c, -1, &py, &pm);
        int pdim = aroma_calendar_days_in_month(py, pm);
        *month_off = -1;
        return pdim - (first - cell - 1);
    }
    if (cell >= first + dim) {
        *month_off = 1;
        return cell - (first + dim) + 1;
    }
    return cell - first + 1;
}


static int cal_pick_year(AromaCalendar *c, int x, int y)
{
    int gx, gy, cw, ch, header_h;
    cal_geom(c, &gx, &gy, &cw, &ch, &header_h);
    int top = c->rect.y + header_h;
    int bottom = c->rect.y + c->rect.height;
    int cell_w = c->rect.width / 3;
    int cell_h = (bottom - top) / 4;
    if (cell_w < 1 || cell_h < 1)
        return -1;
    int sx = c->rect.x + (c->rect.width - cell_w * 3) / 2;
    if (x < sx || x >= sx + cell_w * 3 || y < top || y >= top + cell_h * 4)
        return -1;
    int col = (x - sx) / cell_w;
    int row = (y - top) / cell_h;
    if (col < 0)
        col = 0;
    if (col > 2)
        col = 2;
    if (row < 0)
        row = 0;
    if (row > 3)
        row = 3;
    return c->year_page + row * 3 + col;
}

static void cal_shift(AromaCalendar *c, int delta)
{
    c->month += delta;
    while (c->month < 1) {
        c->month += 12;
        c->year--;
    }
    while (c->month > 12) {
        c->month -= 12;
        c->year++;
    }
    int dim = aroma_calendar_days_in_month(c->year, c->month);
    if (c->selected_day > dim)
        c->selected_day = dim;
}

static void cal_select(AromaNode *node, AromaCalendar *c, int y, int m,
                       int day)
{
    c->year = y;
    c->month = m;
    c->selected_day = day;
    c->has_selection = true;
    if (c->on_select)
        c->on_select(node, y, m, day, c->user_data);
    if (c->popup_open)
        aroma_calendar_close_popup(node);
}

static void cal_enter_years(AromaCalendar *c)
{
    c->show_years = true;
    c->year_page = (c->year / 12) * 12;
}

static void cal_page_years(AromaCalendar *c, int delta)
{
    c->year_page += delta;
    if (c->year_page < 1900)
        c->year_page = 1900;
    if (c->year_page > 2100 - 11)
        c->year_page = 2100 - 11;
}

static void cal_pick_year_apply(AromaNode *node, AromaCalendar *c, int year)
{
    if (year < 1900)
        year = 1900;
    if (year > 2100)
        year = 2100;
    c->year = year;
    int dim = aroma_calendar_days_in_month(c->year, c->month);
    if (c->selected_day > dim)
        c->selected_day = dim;
    c->show_years = false;
    (void)node;
}

#define CAL_POPUP_MAX 8
#define CAL_PANEL_W_DP 340
#define CAL_PANEL_H_DP 376

typedef struct {
    AromaNode *node;
    size_t window_id;
} CalendarOverlayEntry;

static CalendarOverlayEntry g_cal_overlays[CAL_POPUP_MAX];
static size_t g_cal_overlay_count = 0;

static void cal_overlay_register(AromaNode *node, size_t window_id)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_cal_overlay_count; i++) {
        if (g_cal_overlays[i].node == node) {
            g_cal_overlays[i].window_id = window_id;
            return;
        }
    }
    if (g_cal_overlay_count >= CAL_POPUP_MAX)
        return;
    g_cal_overlays[g_cal_overlay_count].node = node;
    g_cal_overlays[g_cal_overlay_count].window_id = window_id;
    g_cal_overlay_count++;
}

static void cal_overlay_unregister(AromaNode *node)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_cal_overlay_count; i++) {
        if (g_cal_overlays[i].node == node) {
            g_cal_overlays[i] = g_cal_overlays[g_cal_overlay_count - 1];
            g_cal_overlay_count--;
            return;
        }
    }
}

static bool cal_field_screen(AromaNode *node, AromaCalendar *c,
                             AromaRect *out)
{
    if (!node || !c || !out)
        return false;
    *out = c->rect;
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

static bool cal_compute_popup(AromaNode *node, AromaCalendar *c)
{
    AromaRect field;
    if (!cal_field_screen(node, c, &field))
        return false;
    int pw = tdp(CAL_PANEL_W_DP);
    int ph = tdp(CAL_PANEL_H_DP);
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
        c->popup_rect.x = (win_w - pw) / 2;
        c->popup_rect.y = (win_h - ph) / 2;
        if (c->popup_rect.x < 0)
            c->popup_rect.x = 0;
        if (c->popup_rect.y < 0)
            c->popup_rect.y = 0;
        c->popup_rect.width = pw;
        c->popup_rect.height = ph;
        return true;
    }
    int px = field.x;
    int py = field.y + field.height + tdp(4);
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    c->popup_rect.x = px;
    c->popup_rect.y = py;
    c->popup_rect.width = pw;
    c->popup_rect.height = ph;
    return true;
}

void aroma_calendar_close_popup(AromaNode *n);

void aroma_datepicker_close_popups(void);

void aroma_calendar_open_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    if (c->popup_open)
        return;
    aroma_datepicker_close_popups();
    aroma_timepicker_close_popups();
    if (!cal_compute_popup(n, c))
        return;
    c->popup_open = true;
    c->show_years = false;
    cal_overlay_register(n, 0);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_calendar_close_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    if (!c->popup_open)
        return;
    c->popup_open = false;
    c->show_years = false;
    cal_overlay_unregister(n);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_calendar_close_popups(void)
{
    while (g_cal_overlay_count > 0) {
        AromaNode *node = g_cal_overlays[0].node;
        if (!node) {
            g_cal_overlays[0] = g_cal_overlays[g_cal_overlay_count - 1];
            g_cal_overlay_count--;
            continue;
        }
        aroma_calendar_close_popup(node);
    }
}

static bool cal_handle_inner(AromaEvent *event, void *ud)
{
    if (!event || !event->target_node)
        return false;
    AromaCalendar *c = (AromaCalendar *)event->target_node->node_widget_ptr;
    if (!c)
        return false;
    int x, y;
    cal_adjust(event, &x, &y);
    bool in = cal_in(x, y, c->rect.x, c->rect.y, c->rect.width,
                     c->rect.height);
    void (*rd)(void *) = (void (*)(void *))ud;
    int slop = tdp(CAL_TOUCH_SLOP_DP);
    switch (event->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
        return in;
    case EVENT_TYPE_MOUSE_EXIT:
        if (c->pressed_day > 0 || c->pressed_nav != -1 ||
            c->pressed_title) {
            c->pressed_day = -1;
            c->pressed_nav = -1;
            c->pressed_title = false;
            aroma_node_invalidate(event->target_node);
        }
        return false;
    case EVENT_TYPE_TOUCH_DOWN: {
        if (!in || c->active_pointer_id != -1)
            return false;
        c->active_pointer_id = event->data.touch.id;
        c->down_x = x;
        c->down_y = y;
        int off = 0;
        bool np = false, nn = false, nt = false;
        int day;
        if (c->show_years) {
            int hh0 = tdp(CAL_HEADER_H_DP);
            int nav = tdp(CAL_MIN_CELL_DP);
            if (y >= c->rect.y && y < c->rect.y + hh0) {
                if (x >= c->rect.x + c->rect.width - 2 * nav &&
                    x < c->rect.x + c->rect.width - nav)
                    np = true;
                else if (x >= c->rect.x + c->rect.width - nav &&
                         x < c->rect.x + c->rect.width)
                    nn = true;
                else if (x >= c->rect.x &&
                         x < c->rect.x + c->rect.width - 2 * nav)
                    nt = true;
                day = (np || nn) ? -2 : (nt ? -3 : -1);
            } else {
                day = cal_pick_year(c, x, y);
            }
        } else {
            day = cal_pick(c, x, y, &off, &np, &nn, &nt);
        }
        c->pressed_day = day;
        c->pressed_month_off = off;
        c->pressed_nav = np ? 0 : (nn ? 1 : -1);
        c->pressed_title = nt;
        if (day != -1)
            aroma_node_invalidate(event->target_node);
        return true;
    }
    case EVENT_TYPE_TOUCH_MOVE:
        if (c->active_pointer_id == -1)
            return false;
        if (c->active_pointer_id != event->data.touch.id)
            return false;
        {
            int dx = x - c->down_x;
            int dy = y - c->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if ((dx >= slop || dy >= slop) &&
                (c->pressed_day != -1 || c->pressed_nav != -1 ||
                 c->pressed_title)) {
                c->pressed_day = -1;
                c->pressed_nav = -1;
                c->pressed_title = false;
                aroma_node_invalidate(event->target_node);
            }
        }
        return true;
    case EVENT_TYPE_TOUCH_UP: {
        if (c->active_pointer_id != event->data.touch.id)
            return false;
        c->active_pointer_id = -1;
        int day = c->pressed_day;
        int off = c->pressed_month_off;
        int nav = c->pressed_nav;
        bool title = c->pressed_title;
        c->pressed_day = -1;
        c->pressed_nav = -1;
        c->pressed_title = false;
        aroma_node_invalidate(event->target_node);
        if (!in || day == -1)
            return true;
        if (c->show_years) {
            bool np2 = false, nn2 = false, nt2 = false;
            int hh0 = tdp(CAL_HEADER_H_DP);
            int navw = tdp(CAL_MIN_CELL_DP);
            if (y >= c->rect.y && y < c->rect.y + hh0) {
                if (x >= c->rect.x + c->rect.width - 2 * navw &&
                    x < c->rect.x + c->rect.width - navw)
                    np2 = true;
                else if (x >= c->rect.x + c->rect.width - navw &&
                         x < c->rect.x + c->rect.width)
                    nn2 = true;
                else if (x >= c->rect.x &&
                         x < c->rect.x + c->rect.width - 2 * navw)
                    nt2 = true;
            }
            if ((np2 && nav == 0) || (nn2 && nav == 1)) {
                cal_page_years(c, np2 ? -12 : 12);
                aroma_node_invalidate(event->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            if (nt2 && title) {
                c->show_years = false;
                aroma_node_invalidate(event->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            int picked = cal_pick_year(c, x, y);
            if (picked > 0 && picked == day) {
                cal_pick_year_apply(event->target_node, c, picked);
                aroma_node_invalidate(event->target_node);
                if (rd)
                    rd(NULL);
            }
            return true;
        }
        int coff;
        bool np, nn, nt;
        int now = cal_pick(c, x, y, &coff, &np, &nn, &nt);
        if ((np && nav == 0) || (nn && nav == 1)) {
            cal_shift(c, np ? -1 : 1);
            aroma_node_invalidate(event->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (nt && title) {
            cal_enter_years(c);
            aroma_node_invalidate(event->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (now != day || coff != off)
            return true;
        if (off != 0) {
            int ny, nm;
            cal_month_year(c, off, &ny, &nm);
            cal_select(event->target_node, c, ny, nm, day);
        } else {
            cal_select(event->target_node, c, c->year, c->month, day);
        }
        aroma_node_invalidate(event->target_node);
        if (rd)
            rd(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_CLICK: {
        if (c->active_pointer_id != -1 || !in)
            break;
        if (c->show_years) {
            int navw = tdp(CAL_MIN_CELL_DP);
            int hh0 = tdp(CAL_HEADER_H_DP);
            if (y >= c->rect.y && y < c->rect.y + hh0) {
                if (x >= c->rect.x + c->rect.width - 2 * navw &&
                    x < c->rect.x + c->rect.width - navw) {
                    cal_page_years(c, -12);
                    aroma_node_invalidate(event->target_node);
                    if (rd)
                        rd(NULL);
                    return true;
                }
                if (x >= c->rect.x + c->rect.width - navw &&
                    x < c->rect.x + c->rect.width) {
                    cal_page_years(c, 12);
                    aroma_node_invalidate(event->target_node);
                    if (rd)
                        rd(NULL);
                    return true;
                }
                c->show_years = false;
                aroma_node_invalidate(event->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            int picked = cal_pick_year(c, x, y);
            if (picked > 0) {
                cal_pick_year_apply(event->target_node, c, picked);
                aroma_node_invalidate(event->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            break;
        }
        int off;
        bool np, nn, nt;
        int day = cal_pick(c, x, y, &off, &np, &nn, &nt);
        if (np || nn) {
            cal_shift(c, np ? -1 : 1);
            aroma_node_invalidate(event->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (nt) {
            cal_enter_years(c);
            aroma_node_invalidate(event->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (day <= 0)
            break;
        if (off != 0) {
            int ny, nm;
            cal_month_year(c, off, &ny, &nm);
            cal_select(event->target_node, c, ny, nm, day);
        } else {
            cal_select(event->target_node, c, c->year, c->month, day);
        }
        aroma_node_invalidate(event->target_node);
        if (rd)
            rd(NULL);
        return true;
    }
    default:
        break;
    }
    return false;
}


static bool cal_handle(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;
    AromaCalendar *c = (AromaCalendar *)e->target_node->node_widget_ptr;
    if (!c)
        return false;
    if (!c->popup_open) {
        switch (e->event_type) {
        case EVENT_TYPE_MOUSE_MOVE:
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_TOUCH_DOWN:
        case EVENT_TYPE_MOUSE_CLICK: {
            int x, y;
            cal_adjust(e, &x, &y);
            if (x < c->rect.x || x >= c->rect.x + c->rect.width ||
                y < c->rect.y || y >= c->rect.y + c->rect.height)
                return false;
            aroma_calendar_open_popup(e->target_node);
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
    bool in_popup = x >= c->popup_rect.x &&
                    x < c->popup_rect.x + c->popup_rect.width &&
                    y >= c->popup_rect.y &&
                    y < c->popup_rect.y + c->popup_rect.height;
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_MOUSE_EXIT:
        if (!in_popup)
            return false;
        break;
    case EVENT_TYPE_TOUCH_DOWN:
    case EVENT_TYPE_MOUSE_CLICK:
        if (!in_popup) {
            aroma_calendar_close_popup(e->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_TOUCH_UP:
    case EVENT_TYPE_TOUCH_MOVE:
        break;
    default:
        return false;
    }
    AromaRect saved = c->rect;
    c->rect = c->popup_rect;
    c->in_popup_event = true;
    bool r = cal_handle_inner(e, ud);
    c->in_popup_event = false;
    c->rect = saved;
    return r;
}

AromaNode *aroma_calendar_create(AromaNode *parent, int x, int y, int width,
                                 int height)
{
    if (!parent)
        return NULL;
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    AromaCalendar *c = (AromaCalendar *)aroma_widget_alloc(sizeof(*c));
    if (!c)
        return NULL;
    memset(c, 0, sizeof(*c));
    c->rect.x = x;
    c->rect.y = y;
    c->rect.width = width > 0 ? width : 300;
    c->rect.height = height > 0 ? height : 280;
    cal_today(&c->year, &c->month, &c->selected_day);
    c->has_selection = false;
    c->pressed_day = -1;
    c->pressed_nav = -1;
    c->show_years = false;
    c->year_page = (c->year / 12) * 12;
    c->use_theme_colors = true;
    c->active_pointer_id = -1;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, c);
    if (!node) {
        aroma_widget_free(c);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_calendar_draw);
    aroma_node_invalidate(node);
    return node;
}

void aroma_calendar_set_date(AromaNode *n, int year, int month, int day)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    if (year >= 1900 && year <= 2100)
        c->year = year;
    if (month >= 1 && month <= 12)
        c->month = month;
    int dim = aroma_calendar_days_in_month(c->year, c->month);
    if (day >= 1 && day <= dim) {
        c->selected_day = day;
        c->has_selection = true;
    }
    aroma_node_invalidate(n);
}

void aroma_calendar_get_date(AromaNode *n, int *year, int *month, int *day)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    if (year)
        *year = c->year;
    if (month)
        *month = c->month;
    if (day)
        *day = c->has_selection ? c->selected_day : -1;
}

void aroma_calendar_set_selected(AromaNode *n, int year, int month, int day)
{
    aroma_calendar_set_date(n, year, month, day);
}

void aroma_calendar_set_year_view(AromaNode *n, bool show_years)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    c->show_years = show_years;
    if (show_years)
        c->year_page = (c->year / 12) * 12;
    aroma_node_invalidate(n);
}

bool aroma_calendar_get_year_view(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    return ((AromaCalendar *)n->node_widget_ptr)->show_years;
}

static void cal_field_text(AromaCalendar *c, char *out, size_t n)
{
    static const char *mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int mi = c->month - 1;
    if (mi < 0 || mi > 11)
        mi = 0;
    if (c->has_selection)
        snprintf(out, n, "%s %d, %d", mon[mi], c->selected_day, c->year);
    else
        snprintf(out, n, "%s %d", mon[mi], c->year);
}

static void cal_draw_field(AromaNode *node, AromaCalendar *c,
                           size_t window_id, AromaGraphicsInterface *gfx)
{
    int pad = tdp(16);
    int glyph = tdp(24);
    char text[32];
    cal_field_text(c, text, sizeof(text));
    gfx->render_text(window_id, c->font, text, c->rect.x + pad,
                     c->rect.y + (c->rect.height -
                                  aroma_font_get_line_height(c->font)) /
                                     2,
                     c->text_color, 1.0f);
    int gx = c->rect.x + c->rect.width - pad - glyph;
    int gy = c->rect.y + (c->rect.height - glyph) / 2;
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, gx, gy, glyph, glyph,
                                   c->dim_color, tdp(1) > 0 ? tdp(1) : 1,
                                   false, tdp_f(3.0f));
    int bar_h = glyph / 4;
    gfx->fill_rectangle(window_id, gx, gy, glyph, bar_h, c->dim_color, false,
                        0.0f);
    int dot = tdp(2);
    if (dot < 1)
        dot = 1;
    gfx->fill_rectangle(window_id, gx + glyph / 4 - dot / 2,
                        gy + bar_h + (glyph - bar_h) / 2 - dot / 2, dot, dot,
                        c->dim_color, true, (float)dot / 2.0f);
    gfx->fill_rectangle(window_id, gx + 3 * glyph / 4 - dot / 2,
                        gy + bar_h + (glyph - bar_h) / 2 - dot / 2, dot, dot,
                        c->dim_color, true, (float)dot / 2.0f);
    (void)node;
}

void aroma_calendar_set_on_select(AromaNode *n, AromaCalendarSelectCb cb,
                                  void *ud)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    c->on_select = cb;
    c->user_data = ud;
}

void aroma_calendar_set_font(AromaNode *n, AromaFont *font)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaCalendar *)n->node_widget_ptr)->font = font;
    aroma_node_invalidate(n);
}

bool aroma_calendar_setup_events(AromaNode *n, void (*on_redraw)(void *),
                                 void *ud)
{
    (void)ud;
    if (!n)
        return false;
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_MOVE, cal_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_EXIT, cal_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_DOWN, cal_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_UP, cal_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_MOVE, cal_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_CLICK, cal_handle,
                          (void *)on_redraw, 90);
    return true;
}

void aroma_calendar_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    AromaCalendar *c = (AromaCalendar *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text)
        return;
    if (!c->font) {
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i].is_active &&
                g_windows[i].window_id == window_id &&
                g_windows[i].default_font) {
                c->font = g_windows[i].default_font;
                break;
            }
        }
    }
    if (c->use_theme_colors) {
        AromaTheme th = aroma_theme_get_global();
        c->bg_color = th.colors.surface;
        c->header_color = th.colors.text_primary;
        c->text_color = th.colors.text_primary;
        c->dim_color = th.colors.text_secondary;
        c->accent_color = th.colors.primary;
        c->accent_text = th.colors.surface;
    }
    gfx->fill_rectangle(window_id, c->rect.x, c->rect.y, c->rect.width,
                        c->rect.height, c->bg_color, true, tdp_f(12.0f));
    if (!c->drawing_popup &&
        gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, c->rect.x, c->rect.y,
                                   c->rect.width, c->rect.height,
                                   c->dim_color, tdp(1) > 0 ? tdp(1) : 1,
                                   true, tdp_f(12.0f));
    if (!c->font)
        return;
    if (!c->drawing_popup) {
        cal_draw_field(node, c, window_id, gfx);
        return;
    }
    static const char *months[12] = {"January", "February", "March",
                                     "April", "May", "June",
                                     "July", "August", "September",
                                     "October", "November", "December"};
    char title[32];
    if (c->show_years)
        snprintf(title, sizeof(title), "%d–%d", c->year_page,
                 c->year_page + 11);
    else
        snprintf(title, sizeof(title), "%s %d",
                 months[c->month - 1 < 0 ? 0 : c->month - 1], c->year);
    int nav = tdp(CAL_MIN_CELL_DP);
    int header_h = tdp(CAL_HEADER_H_DP);
    int th = aroma_font_get_line_height(c->font);
    int chev_r = nav / 2 - tdp(4);
    if (c->pressed_nav == 0) {
        uint32_t shade = aroma_color_blend(c->accent_color, c->bg_color,
                                           0.22f);
        gfx->fill_rectangle(window_id,
                            c->rect.x + c->rect.width - 2 * nav +
                                (nav - 2 * chev_r) / 2,
                            c->rect.y + (header_h - 2 * chev_r) / 2,
                            2 * chev_r, 2 * chev_r, shade, true,
                            (float)chev_r);
    }
    if (c->pressed_nav == 1) {
        uint32_t shade = aroma_color_blend(c->accent_color, c->bg_color,
                                           0.22f);
        gfx->fill_rectangle(window_id,
                            c->rect.x + c->rect.width - nav +
                                (nav - 2 * chev_r) / 2,
                            c->rect.y + (header_h - 2 * chev_r) / 2,
                            2 * chev_r, 2 * chev_r, shade, true,
                            (float)chev_r);
    }
    gfx->render_text(window_id, c->font, title, c->rect.x + tdp(16),
                     c->rect.y + (header_h - th) / 2, c->header_color, 1.1f);
    int chev_w = aroma_font_get_line_width(c->font, "<");
    gfx->render_text(window_id, c->font, "<",
                     c->rect.x + c->rect.width - 2 * nav + (nav - chev_w) / 2,
                     c->rect.y + (header_h - th) / 2, c->header_color, 1.1f);
    chev_w = aroma_font_get_line_width(c->font, ">");
    gfx->render_text(window_id, c->font, ">",
                     c->rect.x + c->rect.width - nav + (nav - chev_w) / 2,
                     c->rect.y + (header_h - th) / 2, c->header_color, 1.1f);

    static const char *wd[7] = {"S", "M", "T", "W", "T", "F", "S"};
    int gx, gy, cw, ch, hh;
    cal_geom(c, &gx, &gy, &cw, &ch, &hh);
    if (c->show_years) {
        int top = c->rect.y + header_h;
        int bottom = c->rect.y + c->rect.height;
        int cell_w = c->rect.width / 3;
        int cell_h = (bottom - top) / 4;
        if (cell_w > 0 && cell_h > 0) {
            int sx = c->rect.x + (c->rect.width - cell_w * 3) / 2;
            int ty2, tm2, td2;
            cal_today(&ty2, &tm2, &td2);
            int ring2 = tdp(2) > 0 ? tdp(2) : 1;
            int pad2 = tdp(12);
            for (int cell = 0; cell < 12; cell++) {
                int year = c->year_page + cell;
                int col = cell % 3;
                int row = cell / 3;
                int cx = sx + col * cell_w;
                int cy = top + row * cell_h;
                bool sel = year == c->year;
                bool today = year == ty2;
                bool pressed = c->pressed_day == year;
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", year);
                int bw = aroma_font_get_line_width(c->font, buf);
                int bh = aroma_font_get_line_height(c->font);
                int ow = bw + 2 * pad2;
                int oh = bh + pad2;
                if (ow > cell_w - 4)
                    ow = cell_w - 4;
                if (oh > cell_h - 4)
                    oh = cell_h - 4;
                int ox = cx + (cell_w - ow) / 2;
                int oyy = cy + (cell_h - oh) / 2;
                if (sel) {
                    gfx->fill_rectangle(window_id, ox, oyy, ow, oh,
                                        c->accent_color, true,
                                        (float)oh / 2.0f);
                } else if (pressed) {
                    uint32_t shade = aroma_color_blend(c->accent_color,
                                                       c->bg_color, 0.25f);
                    gfx->fill_rectangle(window_id, ox, oyy, ow, oh, shade,
                                        true, (float)oh / 2.0f);
                } else if (today && gfx->draw_hollow_rectangle) {
                    gfx->draw_hollow_rectangle(window_id, ox, oyy, ow, oh,
                                               c->accent_color, ring2, true,
                                               (float)oh / 2.0f);
                }
                gfx->render_text(window_id, c->font, buf, cx + (cell_w - bw) / 2,
                                 cy + (cell_h - bh) / 2,
                                 sel ? c->accent_text : c->text_color, 1.0f);
            }
        }
        return;
    }
    for (int i = 0; i < 7; i++) {
        int ww = aroma_font_get_line_width(c->font, wd[i]);
        gfx->render_text(window_id, c->font, wd[i],
                         gx + i * cw + (cw - ww) / 2,
                         c->rect.y + header_h + (tdp(CAL_WEEK_H_DP) - th) / 2,
                         c->dim_color, 0.9f);
    }
    int ty, tm, td;
    cal_today(&ty, &tm, &td);
    int first = aroma_calendar_first_weekday(c->year, c->month);
    int dim = aroma_calendar_days_in_month(c->year, c->month);
    int py, pm;
    cal_month_year(c, -1, &py, &pm);
    int pdim = aroma_calendar_days_in_month(py, pm);
    int r = cw < ch ? cw : ch;
    int inset = tdp(3);
    int ring_w = tdp(2) > 0 ? tdp(2) : 1;
    for (int cell = 0; cell < 42; cell++) {
        int col = cell % 7;
        int row = cell / 7;
        int cx = gx + col * cw;
        int cy = gy + row * ch;
        int day, off;
        if (cell < first) {
            day = pdim - (first - cell - 1);
            off = -1;
        } else if (cell >= first + dim) {
            day = cell - (first + dim) + 1;
            off = 1;
        } else {
            day = cell - first + 1;
            off = 0;
        }
        bool sel = off == 0 && c->has_selection && day == c->selected_day;
        bool today = off == 0 && c->year == ty && c->month == tm && day == td;
        bool pressed = c->pressed_day == day && c->pressed_month_off == off;
        int dia = r - 2 * inset;
        if (dia < 4)
            dia = 4;
        int ox = cx + (cw - r) / 2 + (r - dia) / 2;
        int oy = cy + (ch - r) / 2 + (r - dia) / 2;
        if (sel) {
            gfx->fill_rectangle(window_id, ox, oy, dia, dia,
                                c->accent_color, true, (float)dia / 2.0f);
        } else if (pressed) {
            uint32_t shade = aroma_color_blend(c->accent_color, c->bg_color,
                                               0.25f);
            gfx->fill_rectangle(window_id, ox, oy, dia, dia, shade,
                                true, (float)dia / 2.0f);
        } else if (today && gfx->draw_hollow_rectangle) {
            gfx->draw_hollow_rectangle(window_id, ox, oy, dia, dia,
                                       c->accent_color, ring_w, true,
                                       (float)dia / 2.0f);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", day);
        int bw = aroma_font_get_line_width(c->font, buf);
        int bh = aroma_font_get_line_height(c->font);
        uint32_t colr = sel ? c->accent_text
                            : (off != 0 ? c->dim_color : c->text_color);
        gfx->render_text(window_id, c->font, buf, cx + (cw - bw) / 2,
                         cy + (ch - bh) / 2, colr, 0.95f);
    }
}

void aroma_calendar_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr) {
        AromaCalendar *c = (AromaCalendar *)node->node_widget_ptr;
        if (c->popup_open) {
            c->popup_open = false;
            cal_overlay_unregister(node);
        }
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}



void aroma_calendar_set_popup(AromaNode *n, bool popup)
{
    (void)n;
    (void)popup;
}

bool aroma_calendar_is_popup_open(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    AromaCalendar *c = (AromaCalendar *)n->node_widget_ptr;
    return c->popup_open;
}

bool aroma_calendar_any_popup_open(void)
{
    for (size_t i = 0; i < g_cal_overlay_count; i++) {
        AromaNode *node = g_cal_overlays[i].node;
        if (!node || !node->node_widget_ptr)
            continue;
        AromaCalendar *c = (AromaCalendar *)node->node_widget_ptr;
        if (c->popup_open)
            return true;
    }
    return false;
}

static bool cal_node_visible(AromaNode *node)
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

void aroma_calendar_render_overlays(size_t window_id)
{
    if (g_cal_overlay_count == 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    for (size_t i = 0; i < g_cal_overlay_count;) {
        AromaNode *node = g_cal_overlays[i].node;
        if (!node || !node->node_widget_ptr || !cal_node_visible(node)) {
            if (node)
                cal_overlay_unregister(node);
            else {
                g_cal_overlays[i] = g_cal_overlays[g_cal_overlay_count - 1];
                g_cal_overlay_count--;
            }
            continue;
        }
        AromaCalendar *c = (AromaCalendar *)node->node_widget_ptr;
        if (!c->popup_open) {
            cal_overlay_unregister(node);
            continue;
        }
        if (!cal_compute_popup(node, c)) {
            aroma_calendar_close_popup(node);
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
        AromaRect saved = c->rect;
        c->rect = c->popup_rect;
        c->drawing_popup = true;
        aroma_calendar_draw(node, window_id);
        c->drawing_popup = false;
        c->rect = saved;
        i++;
    }
}

bool aroma_calendar_overlay_hit_test(int x, int y, AromaNode **out_node)
{
    for (size_t i = 0; i < g_cal_overlay_count; i++) {
        AromaNode *node = g_cal_overlays[i].node;
        if (!node || !node->node_widget_ptr || !cal_node_visible(node))
            continue;
        AromaCalendar *c = (AromaCalendar *)node->node_widget_ptr;
        if (!c->popup_open)
            continue;

        (void)x;
        (void)y;
        if (out_node)
            *out_node = node;
        return true;
    }
    return false;
}
