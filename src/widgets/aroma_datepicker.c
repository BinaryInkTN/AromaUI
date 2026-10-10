#include "widgets/aroma_datepicker.h"
#include "widgets/aroma_calendar.h"
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
#include <time.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

/* Material DatePicker dialog, inline: header (title + selected-date
 * headline), month grid with 48dp targets, Cancel/OK footer. Taps edit a
 * pending date (header previews it); OK confirms and fires on_change,
 * Cancel reverts to the confirmed date. Slop-guarded taps throughout. */

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

/* Cross-widget popup exclusivity (calendar/timepicker .c). */
void aroma_calendar_close_popups(void);
void aroma_timepicker_close_popups(void);

#define DP_SLOP_DP 8
#define DP_HEADER_H_DP 96
#define DP_NAV_H_DP 48
#define DP_WEEK_H_DP 28
#define DP_FOOTER_H_DP 52
#define DP_OK_W_DP 76
#define DP_CANCEL_W_DP 96

struct AromaDatePicker {
    AromaRect rect;
    int year;
    int month;
    int day;
    int p_year;
    int p_month;
    int p_day;
    char title[48];
    AromaFont *font;
    bool use_theme_colors;
    uint32_t field_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t dim_color;
    uint32_t accent_color;
    uint32_t accent_text;
    int pressed_day;
    int pressed_off;
    int pressed_nav;
    int pressed_foot;
    bool pressed_title;
    bool show_years;
    int year_page;
    bool popup_open;
    bool drawing_popup;
    bool in_popup_event;
    AromaRect popup_rect;
    AromaDatePickerChangeCb on_change;
    void *user_data;
    int active_pointer_id;
    int down_x;
    int down_y;
};

static void dp_adjust(AromaEvent *e, int *x, int *y)
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
    AromaDatePicker *self =
        t ? (AromaDatePicker *)t->node_widget_ptr : NULL;
    /* Popup panels live in screen space; never compensate scrolling. */
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

static void dp_grid_geom(AromaDatePicker *d, int *gx, int *gy, int *cw,
                         int *ch)
{
    int top = d->rect.y + tdp(DP_HEADER_H_DP + DP_NAV_H_DP + DP_WEEK_H_DP);
    *gx = d->rect.x;
    *cw = d->rect.width / 7;
    if (*cw < 1)
        *cw = 1;
    *gx = d->rect.x + (d->rect.width - (*cw) * 7) / 2;
    *gy = top;
    int bottom = d->rect.y + d->rect.height - tdp(DP_FOOTER_H_DP);
    *ch = (bottom - top) / 6;
    if (*ch < 1)
        *ch = 1;
}

static void dp_month_year(int y, int m, int off, int *oy, int *om)
{
    *oy = y;
    *om = m + off;
    while (*om < 1) {
        *om += 12;
        (*oy)--;
    }
    while (*om > 12) {
        *om -= 12;
        (*oy)++;
    }
}

static void dp_enter_years(AromaDatePicker *d)
{
    d->show_years = true;
    d->year_page = (d->p_year / 12) * 12;
}

static void dp_page_years(AromaDatePicker *d, int delta)
{
    d->year_page += delta;
    if (d->year_page < 1900)
        d->year_page = 1900;
    if (d->year_page > 2100 - 11)
        d->year_page = 2100 - 11;
}

static int dp_pick_year(AromaDatePicker *d, int x, int y)
{
    int header_h = tdp(DP_HEADER_H_DP);
    int nav_h = tdp(DP_NAV_H_DP);
    int footer_h = tdp(DP_FOOTER_H_DP);
    /* Year grid starts below the nav row (same as the day grid minus the
     * weekday row, which years do not need). */
    int top = d->rect.y + header_h + nav_h;
    int bottom = d->rect.y + d->rect.height - footer_h;
    int cell_w = d->rect.width / 3;
    int cell_h = (bottom - top) / 4;
    if (cell_w < 1 || cell_h < 1)
        return -1;
    int sx = d->rect.x + (d->rect.width - cell_w * 3) / 2;
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
    return d->year_page + row * 3 + col;
}

static void dp_apply_year(AromaDatePicker *d, int year)
{
    if (year < 1900)
        year = 1900;
    if (year > 2100)
        year = 2100;
    d->p_year = year;
    int dim = aroma_calendar_days_in_month(d->p_year, d->p_month);
    if (d->p_day > dim)
        d->p_day = dim;
    d->show_years = false;
}

/* Zones: -3 cancel, -2 ok, -1 nav/title row miss, >=1 picked day. */
static int dp_pick(AromaDatePicker *d, int x, int y, int *month_off,
                   bool *nav_prev, bool *nav_next, bool *is_title)
{
    *month_off = 0;
    *nav_prev = false;
    *nav_next = false;
    *is_title = false;
    int header_h = tdp(DP_HEADER_H_DP);
    int nav_h = tdp(DP_NAV_H_DP);
    int footer_h = tdp(DP_FOOTER_H_DP);
    int ok_w = tdp(DP_OK_W_DP);
    int cancel_w = tdp(DP_CANCEL_W_DP);
    if (y >= d->rect.y + d->rect.height - footer_h) {
        if (x >= d->rect.x + d->rect.width - ok_w)
            return -2;
        if (x >= d->rect.x + d->rect.width - ok_w - cancel_w)
            return -3;
        return -1;
    }
    int nav = tdp(48);
    if (y >= d->rect.y + header_h && y < d->rect.y + header_h + nav_h) {
        if (x >= d->rect.x + d->rect.width - 2 * nav &&
            x < d->rect.x + d->rect.width - nav) {
            *nav_prev = true;
            return -2;
        }
        if (x >= d->rect.x + d->rect.width - nav &&
            x < d->rect.x + d->rect.width) {
            *nav_next = true;
            return -2;
        }
        if (x >= d->rect.x &&
            x < d->rect.x + d->rect.width - 2 * nav) {
            *is_title = true;
            return -4;
        }
        return -1;
    }
    int gx, gy, cw, ch;
    dp_grid_geom(d, &gx, &gy, &cw, &ch);
    if (x < gx || x >= gx + cw * 7 || y < gy || y >= gy + ch * 6)
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
    int first = aroma_calendar_first_weekday(d->p_year, d->p_month);
    int dim = aroma_calendar_days_in_month(d->p_year, d->p_month);
    int cell = row * 7 + col;
    if (cell < first) {
        int oy, om;
        dp_month_year(d->p_year, d->p_month, -1, &oy, &om);
        int pdim = aroma_calendar_days_in_month(oy, om);
        *month_off = -1;
        return pdim - (first - cell - 1);
    }
    if (cell >= first + dim) {
        *month_off = 1;
        return cell - (first + dim) + 1;
    }
    return cell - first + 1;
}

static void dp_shift_pending(AromaDatePicker *d, int delta)
{
    int ny, nm;
    dp_month_year(d->p_year, d->p_month, delta, &ny, &nm);
    d->p_year = ny;
    d->p_month = nm;
    int dim = aroma_calendar_days_in_month(ny, nm);
    if (d->p_day > dim)
        d->p_day = dim;
}

static void dp_fire(AromaNode *n, AromaDatePicker *d)
{
    if (d->on_change)
        d->on_change(n, d->year, d->month, d->day, d->user_data);
}

#define DP_POPUP_MAX 8
#define DP_PANEL_W_DP 340
#define DP_PANEL_H_DP 512
#define DP_FIELD_H_DP 56

typedef struct {
    AromaNode *node;
    size_t window_id;
} DatePickerOverlayEntry;

static DatePickerOverlayEntry g_dp_overlays[DP_POPUP_MAX];
static size_t g_dp_overlay_count = 0;

static void dp_overlay_register(AromaNode *node, size_t window_id)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_dp_overlay_count; i++) {
        if (g_dp_overlays[i].node == node) {
            g_dp_overlays[i].window_id = window_id;
            return;
        }
    }
    if (g_dp_overlay_count >= DP_POPUP_MAX)
        return;
    g_dp_overlays[g_dp_overlay_count].node = node;
    g_dp_overlays[g_dp_overlay_count].window_id = window_id;
    g_dp_overlay_count++;
}

static void dp_overlay_unregister(AromaNode *node)
{
    if (!node)
        return;
    for (size_t i = 0; i < g_dp_overlay_count; i++) {
        if (g_dp_overlays[i].node == node) {
            g_dp_overlays[i] = g_dp_overlays[g_dp_overlay_count - 1];
            g_dp_overlay_count--;
            return;
        }
    }
}

/* Field rect in screen coordinates (scroll-compensated). False when the
 * field is scrolled out of every viewport. */
static bool dp_field_screen(AromaNode *node, AromaDatePicker *d,
                            AromaRect *out)
{
    if (!node || !d || !out)
        return false;
    *out = d->rect;
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

static int dp_panel_w(void)
{
    int w = tdp(DP_PANEL_W_DP);
    return w > 0 ? w : 1;
}

static int dp_panel_h(void)
{
    int h = tdp(DP_PANEL_H_DP);
    return h > 0 ? h : 1;
}

/* Center the panel on the window with dp-aware margins, shrinking it
 * to fit small screens instead of spilling past the edges. Falls back
 * to field-anchored placement when the window size is unknown. */
static bool dp_compute_popup(AromaNode *node, AromaDatePicker *d)
{
    AromaRect field;
    if (!dp_field_screen(node, d, &field))
        return false;
    int pw = dp_panel_w();
    int ph = dp_panel_h();
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
        d->popup_rect.x = (win_w - pw) / 2;
        d->popup_rect.y = (win_h - ph) / 2;
        if (d->popup_rect.x < 0)
            d->popup_rect.x = 0;
        if (d->popup_rect.y < 0)
            d->popup_rect.y = 0;
        d->popup_rect.width = pw;
        d->popup_rect.height = ph;
        return true;
    }
    int px = field.x;
    int py = field.y + field.height + tdp(4);
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    d->popup_rect.x = px;
    d->popup_rect.y = py;
    d->popup_rect.width = pw;
    d->popup_rect.height = ph;
    return true;
}

void aroma_datepicker_close_popup(AromaNode *n);

void aroma_calendar_close_popups(void);

void aroma_datepicker_open_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    if (d->popup_open)
        return;
    aroma_calendar_close_popups();
    aroma_timepicker_close_popups();
    if (!dp_compute_popup(n, d))
        return;
    d->popup_open = true;
    d->p_year = d->year;
    d->p_month = d->month;
    d->p_day = d->day;
    d->show_years = false;
    dp_overlay_register(n, 0);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_datepicker_close_popup(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    if (!d->popup_open)
        return;
    d->popup_open = false;
    d->p_year = d->year;
    d->p_month = d->month;
    d->p_day = d->day;
    d->show_years = false;
    dp_overlay_unregister(n);
    aroma_node_invalidate(n);
    aroma_ui_request_redraw(NULL);
}

void aroma_datepicker_close_popups(void)
{
    while (g_dp_overlay_count > 0) {
        AromaNode *node = g_dp_overlays[0].node;
        if (!node) {
            g_dp_overlays[0] = g_dp_overlays[g_dp_overlay_count - 1];
            g_dp_overlay_count--;
            continue;
        }
        aroma_datepicker_close_popup(node);
    }
}

static bool dp_handle_inner(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;
    AromaDatePicker *d = (AromaDatePicker *)e->target_node->node_widget_ptr;
    if (!d)
        return false;
    int x, y;
    dp_adjust(e, &x, &y);
    bool in = x >= d->rect.x && x < d->rect.x + d->rect.width &&
              y >= d->rect.y && y < d->rect.y + d->rect.height;
    void (*rd)(void *) = (void (*)(void *))ud;
    int slop = tdp(DP_SLOP_DP);
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
        return in;
    case EVENT_TYPE_MOUSE_EXIT:
        if (d->pressed_day != -1 || d->pressed_nav != -1 ||
            d->pressed_foot != -1 || d->pressed_title) {
            d->pressed_day = -1;
            d->pressed_nav = -1;
            d->pressed_foot = -1;
            d->pressed_title = false;
            aroma_node_invalidate(e->target_node);
        }
        return false;
    case EVENT_TYPE_TOUCH_DOWN:
        if (!in || d->active_pointer_id != -1)
            return false;
        d->active_pointer_id = e->data.touch.id;
        d->down_x = x;
        d->down_y = y;
        {
            int off = 0;
            bool np = false, nn = false, nt = false;
            int z;
            if (d->show_years) {
                int hh0 = tdp(DP_HEADER_H_DP);
                int navw = tdp(48);
                if (y >= d->rect.y + hh0 &&
                    y < d->rect.y + hh0 + tdp(DP_NAV_H_DP)) {
                    if (x >= d->rect.x + d->rect.width - 2 * navw &&
                        x < d->rect.x + d->rect.width - navw)
                        np = true;
                    else if (x >= d->rect.x + d->rect.width - navw &&
                             x < d->rect.x + d->rect.width)
                        nn = true;
                    else if (x >= d->rect.x &&
                             x < d->rect.x + d->rect.width - 2 * navw)
                        nt = true;
                    z = (np || nn) ? -2 : (nt ? -4 : -1);
                } else {
                    z = dp_pick_year(d, x, y);
                }
            } else {
                z = dp_pick(d, x, y, &off, &np, &nn, &nt);
            }
            d->pressed_day = z;
            d->pressed_off = off;
            d->pressed_nav = np ? 0 : (nn ? 1 : -1);
            d->pressed_title = nt;
            d->pressed_foot = (!np && !nn && !nt && z == -2)   ? 1
                              : ((!np && !nn && !nt && z == -3) ? 0
                                                               : -1);
            if (z != -1)
                aroma_node_invalidate(e->target_node);
        }
        return true;
    case EVENT_TYPE_TOUCH_MOVE:
        if (d->active_pointer_id == -1 ||
            d->active_pointer_id != e->data.touch.id)
            return d->active_pointer_id != -1;
        {
            int dx = x - d->down_x;
            int dy = y - d->down_y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if ((dx >= slop || dy >= slop) &&
                (d->pressed_day != -1 || d->pressed_nav != -1 ||
                 d->pressed_foot != -1 || d->pressed_title)) {
                d->pressed_day = -1;
                d->pressed_nav = -1;
                d->pressed_foot = -1;
                d->pressed_title = false;
                aroma_node_invalidate(e->target_node);
            }
        }
        return true;
    case EVENT_TYPE_TOUCH_UP: {
        if (d->active_pointer_id != e->data.touch.id)
            return false;
        d->active_pointer_id = -1;
        int armed = d->pressed_day;
        int armed_off = d->pressed_off;
        int armed_nav = d->pressed_nav;
        int armed_foot = d->pressed_foot;
        bool armed_title = d->pressed_title;
        d->pressed_day = -1;
        d->pressed_nav = -1;
        d->pressed_foot = -1;
        d->pressed_title = false;
        aroma_node_invalidate(e->target_node);
        if (!in || armed == -1)
            return true;
        if (d->show_years) {
            int hh0 = tdp(DP_HEADER_H_DP);
            int navw = tdp(48);
            bool np2 = false, nn2 = false, nt2 = false;
            if (y >= d->rect.y + hh0 &&
                y < d->rect.y + hh0 + tdp(DP_NAV_H_DP)) {
                if (x >= d->rect.x + d->rect.width - 2 * navw &&
                    x < d->rect.x + d->rect.width - navw)
                    np2 = true;
                else if (x >= d->rect.x + d->rect.width - navw &&
                         x < d->rect.x + d->rect.width)
                    nn2 = true;
                else if (x >= d->rect.x &&
                         x < d->rect.x + d->rect.width - 2 * navw)
                    nt2 = true;
            }
            if ((np2 && armed_nav == 0) || (nn2 && armed_nav == 1)) {
                dp_page_years(d, np2 ? -12 : 12);
                aroma_node_invalidate(e->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            if (nt2 && armed_title) {
                d->show_years = false;
                aroma_node_invalidate(e->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            int picked = dp_pick_year(d, x, y);
            if (picked > 0 && picked == armed) {
                dp_apply_year(d, picked);
                aroma_node_invalidate(e->target_node);
                if (rd)
                    rd(NULL);
            }
            return true;
        }
        int off;
        bool np, nn, nt;
        int z = dp_pick(d, x, y, &off, &np, &nn, &nt);
        if ((np && armed_nav == 0) || (nn && armed_nav == 1)) {
            dp_shift_pending(d, np ? -1 : 1);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (nt && armed_title) {
            dp_enter_years(d);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z == -3 && armed_foot == 0) {
            d->p_year = d->year;
            d->p_month = d->month;
            d->p_day = d->day;
            if (d->popup_open)
                aroma_datepicker_close_popup(e->target_node);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z == -2 && armed_foot == 1) {
            d->year = d->p_year;
            d->month = d->p_month;
            d->day = d->p_day;
            dp_fire(e->target_node, d);
            if (d->popup_open)
                aroma_datepicker_close_popup(e->target_node);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z <= 0 || z != armed || off != armed_off)
            return true;
        if (off != 0) {
            int ny, nm;
            dp_month_year(d->p_year, d->p_month, off, &ny, &nm);
            d->p_year = ny;
            d->p_month = nm;
            d->p_day = z;
        } else {
            d->p_day = z;
        }
        aroma_node_invalidate(e->target_node);
        if (rd)
            rd(NULL);
        return true;
    }
    case EVENT_TYPE_MOUSE_CLICK: {
        if (d->active_pointer_id != -1 || !in)
            break;
        if (d->show_years) {
            int hh0 = tdp(DP_HEADER_H_DP);
            int navw = tdp(48);
            if (y >= d->rect.y + hh0 &&
                y < d->rect.y + hh0 + tdp(DP_NAV_H_DP)) {
                if (x >= d->rect.x + d->rect.width - 2 * navw &&
                    x < d->rect.x + d->rect.width - navw) {
                    dp_page_years(d, -12);
                    aroma_node_invalidate(e->target_node);
                    if (rd)
                        rd(NULL);
                    return true;
                }
                if (x >= d->rect.x + d->rect.width - navw &&
                    x < d->rect.x + d->rect.width) {
                    dp_page_years(d, 12);
                    aroma_node_invalidate(e->target_node);
                    if (rd)
                        rd(NULL);
                    return true;
                }
                d->show_years = false;
                aroma_node_invalidate(e->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            int picked = dp_pick_year(d, x, y);
            if (picked > 0) {
                dp_apply_year(d, picked);
                aroma_node_invalidate(e->target_node);
                if (rd)
                    rd(NULL);
                return true;
            }
            break;
        }
        int off;
        bool np, nn, nt;
        int z = dp_pick(d, x, y, &off, &np, &nn, &nt);
        if (np || nn) {
            dp_shift_pending(d, np ? -1 : 1);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (nt) {
            dp_enter_years(d);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z == -3) {
            d->p_year = d->year;
            d->p_month = d->month;
            d->p_day = d->day;
            if (d->popup_open)
                aroma_datepicker_close_popup(e->target_node);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z == -2) {
            d->year = d->p_year;
            d->month = d->p_month;
            d->day = d->p_day;
            dp_fire(e->target_node, d);
            if (d->popup_open)
                aroma_datepicker_close_popup(e->target_node);
            aroma_node_invalidate(e->target_node);
            if (rd)
                rd(NULL);
            return true;
        }
        if (z <= 0)
            break;
        if (off != 0) {
            int ny, nm;
            dp_month_year(d->p_year, d->p_month, off, &ny, &nm);
            d->p_year = ny;
            d->p_month = nm;
            d->p_day = z;
        } else {
            d->p_day = z;
        }
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

/* Outer dispatcher: routes field taps (open), popup taps (interact via
 * rect-swapped inner handler in screen space) and outside taps
 * (dismiss). */
static bool dp_handle(AromaEvent *e, void *ud)
{
    if (!e || !e->target_node)
        return false;
    AromaDatePicker *d = (AromaDatePicker *)e->target_node->node_widget_ptr;
    if (!d)
        return false;
    if (!d->popup_open) {
        switch (e->event_type) {
        case EVENT_TYPE_MOUSE_MOVE:
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_TOUCH_DOWN:
        case EVENT_TYPE_MOUSE_CLICK: {
            int x, y;
            dp_adjust(e, &x, &y);
            if (x < d->rect.x || x >= d->rect.x + d->rect.width ||
                y < d->rect.y || y >= d->rect.y + d->rect.height)
                return false;
            aroma_datepicker_open_popup(e->target_node);
            return true;
        }
        default:
            return false;
        }
    }
    /* Popup open: everything is modal in raw screen coordinates. */
    int x = e->data.mouse.x;
    int y = e->data.mouse.y;
    if (e->event_type == EVENT_TYPE_TOUCH_DOWN ||
        e->event_type == EVENT_TYPE_TOUCH_UP ||
        e->event_type == EVENT_TYPE_TOUCH_MOVE) {
        x = e->data.touch.x;
        y = e->data.touch.y;
    }
    bool in_popup = x >= d->popup_rect.x &&
                    x < d->popup_rect.x + d->popup_rect.width &&
                    y >= d->popup_rect.y &&
                    y < d->popup_rect.y + d->popup_rect.height;
    switch (e->event_type) {
    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_MOUSE_EXIT:
        if (!in_popup)
            return false;
        break;
    case EVENT_TYPE_TOUCH_DOWN:
    case EVENT_TYPE_MOUSE_CLICK:
        if (!in_popup) {
            aroma_datepicker_close_popup(e->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_TOUCH_UP:
    case EVENT_TYPE_TOUCH_MOVE:
        break;
    default:
        return false;
    }
    AromaRect saved = d->rect;
    d->rect = d->popup_rect;
    d->in_popup_event = true;
    bool r = dp_handle_inner(e, ud);
    d->in_popup_event = false;
    d->rect = saved;
    return r;
}

static void dp_set_all(AromaDatePicker *d, int y, int m, int day)
{
    if (y >= 1900 && y <= 2100)
        d->year = y;
    if (m >= 1 && m <= 12)
        d->month = m;
    int dim = aroma_calendar_days_in_month(d->year, d->month);
    if (day >= 1 && day <= dim)
        d->day = day;
    d->p_year = d->year;
    d->p_month = d->month;
    d->p_day = d->day;
}

AromaNode *aroma_datepicker_create(AromaNode *parent, int x, int y, int width,
                                   int height, int year, int month, int day)
{
    if (!parent)
        return NULL;
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    AromaDatePicker *d = (AromaDatePicker *)aroma_widget_alloc(sizeof(*d));
    if (!d)
        return NULL;
    memset(d, 0, sizeof(*d));
    d->rect.x = x;
    d->rect.y = y;
    d->rect.width = width > 0 ? width : 320;
    d->rect.height = height > 0 ? height : 320;
    d->year = (year >= 1900 && year <= 2100) ? year : 2026;
    d->month = (month >= 1 && month <= 12) ? month : 10;
    int dim = aroma_calendar_days_in_month(d->year, d->month);
    d->day = (day >= 1 && day <= dim) ? day : 1;
    d->p_year = d->year;
    d->p_month = d->month;
    d->p_day = d->day;
    strncpy(d->title, "Select date", sizeof(d->title) - 1);
    d->use_theme_colors = true;
    d->pressed_day = -1;
    d->pressed_nav = -1;
    d->pressed_foot = -1;
    d->pressed_title = false;
    d->show_years = false;
    d->year_page = (d->p_year / 12) * 12;
    d->active_pointer_id = -1;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, d);
    if (!node) {
        aroma_widget_free(d);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_datepicker_draw);
    aroma_node_invalidate(node);
    return node;
}

void aroma_datepicker_set_date(AromaNode *n, int year, int month, int day)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    dp_set_all(d, year, month, day);
    aroma_node_invalidate(n);
}

void aroma_datepicker_get_date(AromaNode *n, int *y, int *m, int *d)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *dp = (AromaDatePicker *)n->node_widget_ptr;
    if (y)
        *y = dp->year;
    if (m)
        *m = dp->month;
    if (d)
        *d = dp->day;
}

void aroma_datepicker_confirm(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    d->year = d->p_year;
    d->month = d->p_month;
    d->day = d->p_day;
    dp_fire(n, d);
    aroma_node_invalidate(n);
}

void aroma_datepicker_cancel(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    d->p_year = d->year;
    d->p_month = d->month;
    d->p_day = d->day;
    aroma_node_invalidate(n);
}

void aroma_datepicker_set_title(AromaNode *n, const char *title)
{
    if (!n || !n->node_widget_ptr || !title)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    strncpy(d->title, title, sizeof(d->title) - 1);
    d->title[sizeof(d->title) - 1] = '\0';
    aroma_node_invalidate(n);
}

void aroma_datepicker_set_year_view(AromaNode *n, bool show_years)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    d->show_years = show_years;
    if (show_years)
        d->year_page = (d->p_year / 12) * 12;
    aroma_node_invalidate(n);
}

bool aroma_datepicker_get_year_view(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    return ((AromaDatePicker *)n->node_widget_ptr)->show_years;
}

void aroma_datepicker_set_on_change(AromaNode *n, AromaDatePickerChangeCb cb,
                                    void *ud)
{
    if (!n || !n->node_widget_ptr)
        return;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    d->on_change = cb;
    d->user_data = ud;
}

void aroma_datepicker_set_font(AromaNode *n, AromaFont *font)
{
    if (!n || !n->node_widget_ptr)
        return;
    ((AromaDatePicker *)n->node_widget_ptr)->font = font;
    aroma_node_invalidate(n);
}

bool aroma_datepicker_setup_events(AromaNode *n, void (*on_redraw)(void *),
                                   void *ud)
{
    (void)ud;
    if (!n)
        return false;
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_MOVE, dp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_EXIT, dp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_DOWN, dp_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_UP, dp_handle,
                          (void *)on_redraw, 90);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_TOUCH_MOVE, dp_handle,
                          (void *)on_redraw, 80);
    aroma_event_subscribe(n->node_id, EVENT_TYPE_MOUSE_CLICK, dp_handle,
                          (void *)on_redraw, 90);
    return true;
}

/* "Tue, Oct 6" headline for the pending date. */
static void dp_headline(AromaDatePicker *d, char *out, size_t n)
{
    static const char *wdn[7] = {"Sun", "Mon", "Tue", "Wed",
                                 "Thu", "Fri", "Sat"};
    static const char *mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = d->p_year - 1900;
    t.tm_mon = d->p_month - 1;
    t.tm_mday = d->p_day;
    mktime(&t);
    int w = t.tm_wday;
    if (w < 0 || w > 6)
        w = 0;
    int mi = d->p_month - 1;
    if (mi < 0 || mi > 11)
        mi = 0;
    snprintf(out, n, "%s, %s %d", wdn[w], mon[mi], d->p_day);
}

/* "Tue, Oct 6" style text for the confirmed date (field + headline). */
static void dp_field_text(AromaDatePicker *d, char *out, size_t n)
{
    static const char *wdn[7] = {"Sun", "Mon", "Tue", "Wed",
                                 "Thu", "Fri", "Sat"};
    static const char *mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = d->year - 1900;
    t.tm_mon = d->month - 1;
    t.tm_mday = d->day;
    mktime(&t);
    int w = t.tm_wday;
    if (w < 0 || w > 6)
        w = 0;
    int mi = d->month - 1;
    if (mi < 0 || mi > 11)
        mi = 0;
    snprintf(out, n, "%s, %s %d", wdn[w], mon[mi], d->day);
}

/* Compact in-tree field for popup mode: formatted date + calendar glyph. */
static void dp_draw_field(AromaNode *node, AromaDatePicker *d,
                          size_t window_id, AromaGraphicsInterface *gfx)
{
    int pad = tdp(16);
    int glyph = tdp(24);
    char text[32];
    dp_field_text(d, text, sizeof(text));
    gfx->render_text(window_id, d->font, text, d->rect.x + pad,
                     d->rect.y + (d->rect.height -
                                  aroma_font_get_line_height(d->font)) /
                                     2,
                     d->text_color, 1.0f);
    /* Calendar glyph: outline + header bar + two day dots. */
    int gx = d->rect.x + d->rect.width - pad - glyph;
    int gy = d->rect.y + (d->rect.height - glyph) / 2;
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, gx, gy, glyph, glyph,
                                   d->dim_color, tdp(1) > 0 ? tdp(1) : 1,
                                   false, tdp_f(3.0f));
    int bar_h = glyph / 4;
    gfx->fill_rectangle(window_id, gx, gy, glyph, bar_h, d->dim_color, false,
                        0.0f);
    int dot = tdp(2);
    if (dot < 1)
        dot = 1;
    gfx->fill_rectangle(window_id, gx + glyph / 4 - dot / 2,
                        gy + bar_h + (glyph - bar_h) / 2 - dot / 2, dot, dot,
                        d->dim_color, true, (float)dot / 2.0f);
    gfx->fill_rectangle(window_id, gx + 3 * glyph / 4 - dot / 2,
                        gy + bar_h + (glyph - bar_h) / 2 - dot / 2, dot, dot,
                        d->dim_color, true, (float)dot / 2.0f);
    (void)node;
}

void aroma_datepicker_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    AromaDatePicker *d = (AromaDatePicker *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text)
        return;
    if (!d->font) {
        for (int i = 0; i < g_window_count; i++) {
            if (g_windows[i].is_active &&
                g_windows[i].window_id == window_id &&
                g_windows[i].default_font) {
                d->font = g_windows[i].default_font;
                break;
            }
        }
    }
    if (d->use_theme_colors) {
        AromaTheme th = aroma_theme_get_global();
        d->field_color = th.colors.surface;
        d->border_color = th.colors.border;
        d->text_color = th.colors.text_primary;
        d->dim_color = th.colors.text_secondary;
        d->accent_color = th.colors.primary;
        d->accent_text = th.colors.surface;
    }
    gfx->fill_rectangle(window_id, d->rect.x, d->rect.y, d->rect.width,
                        d->rect.height, d->field_color, true, tdp_f(12.0f));
    if (gfx->draw_hollow_rectangle)
        gfx->draw_hollow_rectangle(window_id, d->rect.x, d->rect.y,
                                    d->rect.width, d->rect.height,
                                    d->border_color, tdp(1) > 0 ? tdp(1) : 1,
                                    true, tdp_f(12.0f));
    if (!d->font)
        return;
    if (!d->drawing_popup) {
        dp_draw_field(node, d, window_id, gfx);
        return;
    }
    int header_h = tdp(DP_HEADER_H_DP);
    int nav_h = tdp(DP_NAV_H_DP);
    int footer_h = tdp(DP_FOOTER_H_DP);
    int pad = tdp(16);
    gfx->render_text(window_id, d->font, d->title, d->rect.x + pad,
                     d->rect.y + tdp(12), d->dim_color, 0.9f);
    char head[32];
    dp_headline(d, head, sizeof(head));
    gfx->render_text(window_id, d->font, head, d->rect.x + pad,
                     d->rect.y + tdp(36), d->text_color, 1.5f);
    if (gfx->draw_line)
        gfx->draw_line(window_id, d->rect.x + pad, d->rect.y + header_h - 1,
                       d->rect.x + d->rect.width - pad,
                       d->rect.y + header_h - 1, d->border_color,
                       tdp_f(1.0f) > 0.0f ? tdp_f(1.0f) : 1.0f, false);

    static const char *months[12] = {"January", "February", "March",
                                     "April", "May", "June",
                                     "July", "August", "September",
                                     "October", "November", "December"};
    char mtitle[32];
    if (d->show_years)
        snprintf(mtitle, sizeof(mtitle), "%d–%d", d->year_page,
                 d->year_page + 11);
    else
        snprintf(mtitle, sizeof(mtitle), "%s %d", months[d->p_month - 1],
                 d->p_year);
    int nav = tdp(48);
    int lh = aroma_font_get_line_height(d->font);
    int chev_r = nav / 2 - tdp(4);
    if (d->pressed_nav == 0 || d->pressed_nav == 1) {
        uint32_t shade = aroma_color_blend(d->accent_color, d->field_color,
                                           0.22f);
        int zx = d->rect.x + d->rect.width -
                 (d->pressed_nav == 0 ? 2 * nav : nav);
        gfx->fill_rectangle(window_id, zx + (nav - 2 * chev_r) / 2,
                            d->rect.y + header_h + (nav_h - 2 * chev_r) / 2,
                            2 * chev_r, 2 * chev_r, shade, true,
                            (float)chev_r);
    }
    int chev_prev = aroma_font_get_line_width(d->font, "<");
    int chev_next = aroma_font_get_line_width(d->font, ">");
    gfx->render_text(window_id, d->font, mtitle, d->rect.x + pad,
                     d->rect.y + header_h + (nav_h - lh) / 2, d->text_color,
                     1.0f);
    gfx->render_text(window_id, d->font, "<",
                     d->rect.x + d->rect.width - 2 * nav +
                         (nav - chev_prev) / 2,
                     d->rect.y + header_h + (nav_h - lh) / 2, d->text_color,
                     1.0f);
    gfx->render_text(window_id, d->font, ">",
                     d->rect.x + d->rect.width - nav + (nav - chev_next) / 2,
                     d->rect.y + header_h + (nav_h - lh) / 2, d->text_color,
                     1.0f);

    static const char *wd[7] = {"S", "M", "T", "W", "T", "F", "S"};
    int gx, gy, cw, ch;
    dp_grid_geom(d, &gx, &gy, &cw, &ch);
    if (d->show_years) {
        int top = d->rect.y + header_h + nav_h;
        int bottom = d->rect.y + d->rect.height - footer_h;
        int cell_w = d->rect.width / 3;
        int cell_h = (bottom - top) / 4;
        if (cell_w > 0 && cell_h > 0) {
            int sx = d->rect.x + (d->rect.width - cell_w * 3) / 2;
            int ty0 = 0, tm0 = 0, td0 = 0;
            {
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                if (t)
                    ty0 = t->tm_year + 1900;
                else
                    ty0 = 2026;
                (void)tm0;
                (void)td0;
            }
            int ring0 = tdp(2) > 0 ? tdp(2) : 1;
            int pad0 = tdp(12);
            for (int cell = 0; cell < 12; cell++) {
                int year = d->year_page + cell;
                int col = cell % 3;
                int row = cell / 3;
                int cx = sx + col * cell_w;
                int cy = top + row * cell_h;
                bool sel = year == d->p_year;
                bool today = year == ty0;
                bool pressed = d->pressed_day == year;
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", year);
                int bw = aroma_font_get_line_width(d->font, buf);
                int bh = aroma_font_get_line_height(d->font);
                int ow = bw + 2 * pad0;
                int oh = bh + pad0;
                if (ow > cell_w - 4)
                    ow = cell_w - 4;
                if (oh > cell_h - 4)
                    oh = cell_h - 4;
                int ox = cx + (cell_w - ow) / 2;
                int oyy = cy + (cell_h - oh) / 2;
                if (sel) {
                    gfx->fill_rectangle(window_id, ox, oyy, ow, oh,
                                        d->accent_color, true,
                                        (float)oh / 2.0f);
                } else if (pressed) {
                    uint32_t shade = aroma_color_blend(d->accent_color,
                                                       d->field_color,
                                                       0.25f);
                    gfx->fill_rectangle(window_id, ox, oyy, ow, oh, shade,
                                        true, (float)oh / 2.0f);
                } else if (today && gfx->draw_hollow_rectangle) {
                    gfx->draw_hollow_rectangle(window_id, ox, oyy, ow, oh,
                                               d->accent_color, ring0, true,
                                               (float)oh / 2.0f);
                }
                gfx->render_text(window_id, d->font, buf,
                                 cx + (cell_w - bw) / 2,
                                 cy + (cell_h - bh) / 2,
                                 sel ? d->accent_text : d->text_color, 1.0f);
            }
        }
    } else {
    for (int i = 0; i < 7; i++) {
        int ww = aroma_font_get_line_width(d->font, wd[i]);
        gfx->render_text(window_id, d->font, wd[i],
                         gx + i * cw + (cw - ww) / 2,
                         d->rect.y + header_h + nav_h + tdp(4),
                         d->dim_color, 0.85f);
    }
    int ty, tm, td;
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        if (t) {
            ty = t->tm_year + 1900;
            tm = t->tm_mon + 1;
            td = t->tm_mday;
        } else {
            ty = 2026;
            tm = 10;
            td = 6;
        }
    }
    int first = aroma_calendar_first_weekday(d->p_year, d->p_month);
    int dim = aroma_calendar_days_in_month(d->p_year, d->p_month);
    int oy, om;
    dp_month_year(d->p_year, d->p_month, -1, &oy, &om);
    int pdim = aroma_calendar_days_in_month(oy, om);
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
        bool sel = off == 0 && day == d->p_day;
        bool today = off == 0 && d->p_year == ty && d->p_month == tm &&
                     day == td;
        bool pressed = d->pressed_day == day && d->pressed_off == off;
        int dia = r - 2 * inset;
        if (dia < 4)
            dia = 4;
        int ox = cx + (cw - r) / 2 + (r - dia) / 2;
        int oy = cy + (ch - r) / 2 + (r - dia) / 2;
        if (sel) {
            gfx->fill_rectangle(window_id, ox, oy, dia, dia,
                                d->accent_color, true,
                                (float)dia / 2.0f);
        } else if (pressed) {
            uint32_t shade = aroma_color_blend(d->accent_color,
                                               d->field_color, 0.25f);
            gfx->fill_rectangle(window_id, ox, oy, dia, dia, shade,
                                true, (float)dia / 2.0f);
        } else if (today && gfx->draw_hollow_rectangle) {
            gfx->draw_hollow_rectangle(window_id, ox, oy, dia, dia,
                                       d->accent_color, ring_w, true,
                                       (float)dia / 2.0f);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", day);
        int bw = aroma_font_get_line_width(d->font, buf);
        int bh = aroma_font_get_line_height(d->font);
        gfx->render_text(window_id, d->font, buf, cx + (cw - bw) / 2,
                         cy + (ch - bh) / 2,
                         sel ? d->accent_text
                             : (off ? d->dim_color : d->text_color),
                         0.9f);
    }
    }
    int fy = d->rect.y + d->rect.height - footer_h;
    int ok_w = tdp(DP_OK_W_DP);
    int cancel_w = tdp(DP_CANCEL_W_DP);
    char *cancel = "Cancel";
    char *ok = "OK";
    int chw = aroma_font_get_line_width(d->font, cancel);
    int ohw = aroma_font_get_line_width(d->font, ok);
    int pill_pad = tdp(6);
    if (d->pressed_foot == 0 || d->pressed_foot == 1) {
        uint32_t shade = aroma_color_blend(d->accent_color, d->field_color,
                                           0.22f);
        int zx = d->rect.x + d->rect.width - ok_w - cancel_w +
                 (d->pressed_foot == 0 ? 0 : cancel_w);
        int zw = d->pressed_foot == 0 ? cancel_w : ok_w;
        gfx->fill_rectangle(window_id, zx + pill_pad / 2,
                            fy + pill_pad / 2, zw - pill_pad,
                            footer_h - pill_pad, shade, true,
                            tdp_f(8.0f));
    }
    gfx->render_text(window_id, d->font, cancel,
                     d->rect.x + d->rect.width - ok_w - cancel_w +
                         (cancel_w - chw) / 2,
                     fy + (footer_h - lh) / 2, d->accent_color, 1.0f);
    gfx->render_text(window_id, d->font, ok,
                     d->rect.x + d->rect.width - ok_w + (ok_w - ohw) / 2,
                     fy + (footer_h - lh) / 2, d->accent_color, 1.0f);
}

void aroma_datepicker_destroy(AromaNode *node)
{
    if (!node)
        return;
    if (node->node_widget_ptr) {
        AromaDatePicker *d = (AromaDatePicker *)node->node_widget_ptr;
        if (d->popup_open) {
            d->popup_open = false;
            dp_overlay_unregister(node);
        }
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }
}

/* Popup-only widget: every date picker is a compact field that opens a
 * modal panel. Kept for source compatibility; always a no-op. */
void aroma_datepicker_set_popup(AromaNode *n, bool popup)
{
    (void)n;
    (void)popup;
}

bool aroma_datepicker_is_popup_open(AromaNode *n)
{
    if (!n || !n->node_widget_ptr)
        return false;
    AromaDatePicker *d = (AromaDatePicker *)n->node_widget_ptr;
    return d->popup_open;
}

bool aroma_datepicker_any_popup_open(void)
{
    for (size_t i = 0; i < g_dp_overlay_count; i++) {
        AromaNode *node = g_dp_overlays[i].node;
        if (!node || !node->node_widget_ptr)
            continue;
        AromaDatePicker *d = (AromaDatePicker *)node->node_widget_ptr;
        if (d->popup_open)
            return true;
    }
    return false;
}

static bool dp_node_visible(AromaNode *node)
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

void aroma_datepicker_render_overlays(size_t window_id)
{
    if (g_dp_overlay_count == 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    for (size_t i = 0; i < g_dp_overlay_count;) {
        AromaNode *node = g_dp_overlays[i].node;
        if (!node || !node->node_widget_ptr ||
            !dp_node_visible(node)) {
            if (node)
                dp_overlay_unregister(node);
            else {
                g_dp_overlays[i] = g_dp_overlays[g_dp_overlay_count - 1];
                g_dp_overlay_count--;
            }
            continue;
        }
        AromaDatePicker *d = (AromaDatePicker *)node->node_widget_ptr;
        if (!d->popup_open) {
            dp_overlay_unregister(node);
            continue;
        }
        if (!dp_compute_popup(node, d)) {
            aroma_datepicker_close_popup(node);
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
        AromaRect saved = d->rect;
        d->rect = d->popup_rect;
        d->drawing_popup = true;
        aroma_datepicker_draw(node, window_id);
        d->drawing_popup = false;
        d->rect = saved;
        i++;
    }
}

bool aroma_datepicker_overlay_hit_test(int x, int y, AromaNode **out_node)
{
    for (size_t i = 0; i < g_dp_overlay_count; i++) {
        AromaNode *node = g_dp_overlays[i].node;
        if (!node || !node->node_widget_ptr || !dp_node_visible(node))
            continue;
        AromaDatePicker *d = (AromaDatePicker *)node->node_widget_ptr;
        if (!d->popup_open)
            continue;
        /* Modal popup: every tap routes to the picker (inside interacts,
         * outside dismisses). */
        (void)x;
        (void)y;
        if (out_node)
            *out_node = node;
        return true;
    }
    return false;
}
