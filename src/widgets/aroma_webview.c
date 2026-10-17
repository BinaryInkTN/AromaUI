#include "widgets/aroma_webview.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_dp.h"
#include "aroma_time.h"
#include "aroma_timer.h"
#include "aroma_ui.h"
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#ifdef AROMA_HAS_WEBVIEW
#include "widgets/aroma_webview_render.h"
#endif

#define AROMA_WEBVIEW_URL_MAX 1024
#define AROMA_WEBVIEW_MAX_BRIDGES 8

typedef struct {
    AromaNode *node;
    char name[64];
    AromaWebViewJsBridgeCb cb;
    void *user;
} AromaWebBridgeEntry;

typedef struct AromaWebView {
    AromaRect rect;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebPage *page;
#endif
    unsigned int texture_id;
    bool owns_texture;
    int tex_w;
    int tex_h;
    int scroll_x;
    int scroll_y;
    bool need_render;
    bool need_upload;
    // Bumped on every render request; the async landing only accepts a
    // worker frame whose job sequence equals this value (stale frames from
    // an older scroll/page are dropped, never uploaded).
    unsigned render_seq;
    bool has_error;
    char error_msg[256];
    char last_url[AROMA_WEBVIEW_URL_MAX];
    char *last_html;
    char last_base[AROMA_WEBVIEW_URL_MAX];
    AromaWebViewNavigateCb on_navigate;
    void *navigate_ud;
    AromaWebViewTitleCb on_title;
    void *title_ud;
    AromaWebViewErrorCb on_error;
    void *error_ud;
    AromaWebViewLoadCb on_load;
    void *load_ud;
    AromaWebBridgeEntry bridges[AROMA_WEBVIEW_MAX_BRIDGES];
    int bridge_count;
    AromaTimer *js_timer;
    int active_pointer_id;
    int press_x;
    int press_y;
    int press_scroll_x;
    int press_scroll_y;
    bool dragging;
    bool mouse_down;
    int mouse_x;
    int mouse_y;
    // Touch velocity tracking for fling (CSS px + ms timestamps, ring).
    int vel_px[4];
    int vel_py[4];
    uint64_t vel_t[4];
    int vel_count;
    // Inertial scroll state (CSS px/sec + fractional remainder).
    float fling_vx;
    float fling_vy;
    float fling_fx;
    float fling_fy;
    AromaTimer *fling_timer;
    bool events_registered;
    // True while we believe the platform IME is up because a page control
    // has focus; used to hide it again on blur/destroy.
    bool keyboard_shown;
} AromaWebView;

#ifdef __ANDROID__
static inline int webview_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float webview_density(void) {
    float d = aroma_android_get_density();
    return (d > 0.1f) ? d : 1.0f;
}
static inline float webview_device_scale(void) { return webview_density(); }
#else
static inline int webview_dp(int dp) { return dp; }
static inline float webview_density(void) { return 1.0f; }
static inline float webview_device_scale(void) { return 1.0f; }
#endif

// Viewport in CSS px (dp on Android) for a physical-px rect.
static inline int webview_css_w(int phys_w)
{
#ifdef __ANDROID__
    float s = webview_device_scale();
    if (s > 0.1f)
        return (int)(phys_w / s + 0.5f);
#endif
    return phys_w;
}

static inline int webview_css_h(int phys_h)
{
#ifdef __ANDROID__
    float s = webview_device_scale();
    if (s > 0.1f)
        return (int)(phys_h / s + 0.5f);
#endif
    return phys_h;
}

static void webview_request_render(AromaNode *node);
static void webview_after_navigate(AromaNode *node);
static void webview_fling_stop(AromaWebView *v);

// One-shot stage diagnostics for a blank webview (white rectangle): logs
// exactly once per process which ensure_render stage failed, with the
// geometry that decides it. Temporary aid for on-device debugging.
static void webview_diag(AromaNode *node, const char *stage)
{
    static bool logged = false;
    if (logged)
        return;
    logged = true;
    AromaWebView *v = (node && node->node_widget_ptr)
        ? (AromaWebView *)node->node_widget_ptr : NULL;
    if (!v) {
        LOG_ERROR("aroma_webview: diag stage=%s (no widget)", stage);
        return;
    }
#ifdef AROMA_HAS_WEBVIEW
    int cw = 0, ch = 0, pw = 0, ph = 0;
    if (v->page) {
        aroma_webpage_content_size(v->page, &cw, &ch);
        aroma_webpage_pixels(v->page, &pw, &ph);
    }
    LOG_ERROR("aroma_webview: diag stage=%s rect=(%d,%d,%dx%d) scroll=(%d,%d) content=(%dx%d) pagebuf=(%dx%d) scale=%.3f tex=%u need_up=%d has_page=%d",
              stage, v->rect.x, v->rect.y, v->rect.width, v->rect.height,
              v->scroll_x, v->scroll_y, cw, ch, pw, ph,
              webview_device_scale(), v->texture_id, (int)v->need_upload,
              (int)(v->page != NULL));
#else
    LOG_ERROR("aroma_webview: diag stage=%s rect=(%d,%d,%dx%d) (webview support not compiled)",
              stage, v->rect.x, v->rect.y, v->rect.width, v->rect.height);
#endif
}

static void webview_report_error(AromaWebView *v, const char *msg)
{
    if (!v)
        return;
    v->has_error = true;
    if (msg) {
        strncpy(v->error_msg, msg, sizeof(v->error_msg) - 1);
        v->error_msg[sizeof(v->error_msg) - 1] = '\0';
    } else {
        v->error_msg[0] = '\0';
    }
    LOG_ERROR("aroma_webview: %s", v->error_msg[0] ? v->error_msg : "unknown error");
}

static void webview_clamp_scroll(AromaWebView *v)
{
    if (v->scroll_x < 0)
        v->scroll_x = 0;
    if (v->scroll_y < 0)
        v->scroll_y = 0;
#ifdef AROMA_HAS_WEBVIEW
    if (v->page) {
        int cw = 0, ch = 0;
        aroma_webpage_content_size(v->page, &cw, &ch);
        // Content and scroll are in CSS px (dp); rect is physical px.
        int vw = webview_css_w(v->rect.width);
        int vh = webview_css_h(v->rect.height);
        int mx = cw - vw;
        int my = ch - vh;
        if (mx < 0)
            mx = 0;
        if (my < 0)
            my = 0;
        if (v->scroll_x > mx)
            v->scroll_x = mx;
        if (v->scroll_y > my)
            v->scroll_y = my;
    }
#endif
}

static void webview_request_render(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    v->render_seq++;
    v->need_render = true;
    v->need_upload = true;
    aroma_node_invalidate(node);
}

// Defined after webview_upload(): consumes a finished async frame when it
// matches the latest request, uploads it and wakes the widget.
static bool webview_try_land_async(AromaNode *node);

static void webview_sync_after_page(AromaNode *node, unsigned gen_before)
{
    if (!node || !node->node_widget_ptr)
        return;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page)
        return;
    if (aroma_webpage_nav_gen(v->page) != gen_before) {
        webview_after_navigate(node);
    } else if (aroma_webpage_dom_dirty(v->page)) {
        webview_request_render(node);
    }
#else
    (void)gen_before;
#endif
}

#ifdef AROMA_HAS_WEBVIEW
static void webview_js_timer_cb(void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page)
        return;
    unsigned gen = aroma_webpage_nav_gen(v->page);
    bool fired = aroma_webpage_run_timers(v->page);
    if (fired || aroma_webpage_nav_gen(v->page) != gen)
        webview_sync_after_page(node, gen);
    else if (aroma_webpage_dom_dirty(v->page))
        webview_request_render(node);
    // Async rasters finish on the worker thread; this periodic UI-thread
    // tick is the wake-up that turns a finished frame into a repaint while
    // the widget is idle (draws only run for invalidated nodes).
    if (!v->need_render && !v->need_upload)
        return;
    if (aroma_webpage_async_ready(v->page))
        aroma_node_invalidate(node);
}
#endif

static void webview_after_navigate(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    webview_fling_stop(v);
    v->scroll_x = 0;
    v->scroll_y = 0;
    v->has_error = false;
    v->error_msg[0] = '\0';
    webview_request_render(node);
    const char *url = aroma_webview_get_url(node);
    const char *title = aroma_webview_get_title(node);
    if (v->on_navigate && url)
        v->on_navigate(node, url, v->navigate_ud);
    if (v->on_title && title)
        v->on_title(node, title, v->title_ud);
    if (v->on_load)
        v->on_load(node, v->load_ud);
}

static bool webview_point_in(AromaWebView *v, int x, int y)
{
    return v && x >= v->rect.x && x <= v->rect.x + v->rect.width &&
           y >= v->rect.y && y <= v->rect.y + v->rect.height;
}

// Sync the platform IME with the page: show it whenever an editable control
// holds page focus (so soft keyboards can type into inputs/textareas), hide
// it otherwise. Safe no-op on platforms without an IME.
static void webview_sync_keyboard(AromaWebView *v)
{
    if (!v)
        return;
    bool want = false;
#ifdef AROMA_HAS_WEBVIEW
    want = v->page && aroma_webpage_has_focus_control(v->page);
#endif
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (want) {
        if (platform && platform->show_keyboard) {
            platform->show_keyboard();
            v->keyboard_shown = true;
        }
        return;
    }
    if (v->keyboard_shown) {
        if (platform && platform->hide_keyboard)
            platform->hide_keyboard();
        v->keyboard_shown = false;
    }
}

static void webview_hide_keyboard(AromaWebView *v)
{
    if (!v || !v->keyboard_shown)
        return;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->hide_keyboard)
        platform->hide_keyboard();
    v->keyboard_shown = false;
}

static void webview_do_click(AromaNode *node, int x, int y)
{
#ifdef AROMA_HAS_WEBVIEW
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page || !webview_point_in(v, x, y))
        return;
    unsigned gen = aroma_webpage_nav_gen(v->page);
    // Events arrive in physical px; layout/click targets are in CSS px.
    float ds = webview_device_scale();
    int css_x, css_y;
    if (ds < 0.01f || ds == 1.0f) {
        css_x = x - v->rect.x;
        css_y = y - v->rect.y;
    } else {
        css_x = (int)(((float)(x - v->rect.x)) / ds + 0.5f);
        css_y = (int)(((float)(y - v->rect.y)) / ds + 0.5f);
    }
    int cx = css_x + v->scroll_x;
    int cy = css_y + v->scroll_y;
    if (aroma_webpage_click(v->page, cx, cy))
        webview_after_navigate(node);
    else
        webview_sync_after_page(node, gen);
    // Tapping an input opens the IME; tapping elsewhere closes it.
    webview_sync_keyboard(v);
#else
    (void)node;
    (void)x;
    (void)y;
#endif
}

static void webview_do_scroll(AromaNode *node, int dx, int dy)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!dx && !dy)
        return;
    v->scroll_x += dx;
    v->scroll_y += dy;
    webview_clamp_scroll(v);
    webview_request_render(node);
}

static void webview_fling_stop(AromaWebView *v)
{
    if (!v)
        return;
    if (v->fling_timer) {
        aroma_timer_cancel(v->fling_timer);
        v->fling_timer = NULL;
    }
    v->fling_vx = 0.0f;
    v->fling_vy = 0.0f;
    v->fling_fx = 0.0f;
    v->fling_fy = 0.0f;
}

// Inertial scroll step: exponential friction, stops at content edges or
// when slow. Runs at ~60Hz via a repeating timer.
static void webview_fling_cb(void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    // A new touch gesture preempts the fling.
    if (v->active_pointer_id != -1 || v->mouse_down) {
        webview_fling_stop(v);
        return;
    }
    const float dt = 1.0f / 60.0f;
    v->fling_vx *= 0.94f;
    v->fling_vy *= 0.94f;
    float speed = v->fling_vx >= 0.0f ? v->fling_vx : -v->fling_vx;
    float speedy = v->fling_vy >= 0.0f ? v->fling_vy : -v->fling_vy;
    if (speedy > speed)
        speed = speedy;
    if (speed < 40.0f) {
        webview_fling_stop(v);
        return;
    }
    v->fling_fx += v->fling_vx * dt;
    v->fling_fy += v->fling_vy * dt;
    int dx = (int)v->fling_fx;
    int dy = (int)v->fling_fy;
    v->fling_fx -= (float)dx;
    v->fling_fy -= (float)dy;
    if (!dx && !dy)
        return;
    int before_x = v->scroll_x, before_y = v->scroll_y;
    v->scroll_x += dx;
    v->scroll_y += dy;
    webview_clamp_scroll(v);
    // Hit an edge along an axis: kill that axis so we don't grind the edge.
    if (v->scroll_x == before_x && (dx > 0) == (v->fling_vx > 0.0f))
        v->fling_vx = 0.0f;
    if (v->scroll_y == before_y && (dy > 0) == (v->fling_vy > 0.0f))
        v->fling_vy = 0.0f;
    if (v->fling_vx == 0.0f && v->fling_vy == 0.0f) {
        webview_fling_stop(v);
        return;
    }
    webview_request_render(node);
}

static void webview_vel_reset(AromaWebView *v)
{
    v->vel_count = 0;
    v->fling_vx = 0.0f;
    v->fling_vy = 0.0f;
    v->fling_fx = 0.0f;
    v->fling_fy = 0.0f;
}

static void webview_vel_push(AromaWebView *v, int x, int y)
{
    uint64_t now = aroma_time_now_ms();
    if (v->vel_count > 0) {
        int last = (v->vel_count - 1) % 4;
        if (v->vel_px[last] == x && v->vel_py[last] == y)
            return; // stationary event adds no information
    }
    int i = v->vel_count % 4;
    v->vel_px[i] = x;
    v->vel_py[i] = y;
    v->vel_t[i] = now;
    v->vel_count++;
}

// Start inertial scrolling from the recent touch velocity (CSS px/sec).
// Returns false when the gesture was too slow to fling.
static bool webview_fling_start(AromaNode *node, AromaWebView *v, float density)
{
    int n = v->vel_count < 4 ? v->vel_count : 4;
    if (n < 2)
        return false;
    int first = (v->vel_count - n) % 4;
    int last = (v->vel_count - 1) % 4;
    uint64_t dt_ms = 0;
    if (v->vel_t[last] > v->vel_t[first])
        dt_ms = v->vel_t[last] - v->vel_t[first];
    if (dt_ms < 16 || dt_ms > 1000)
        return false;
    float ds = (density < 0.01f || density == 1.0f) ? 1.0f : density;
    float dx_css = (float)(v->vel_px[last] - v->vel_px[first]) / ds;
    float dy_css = (float)(v->vel_py[last] - v->vel_py[first]) / ds;
    float inv_dt = 1000.0f / (float)dt_ms;
    float vx = dx_css * inv_dt;
    float vy = dy_css * inv_dt;
    float speed = vx >= 0.0f ? vx : -vx;
    float speedy = vy >= 0.0f ? vy : -vy;
    if (speedy > speed)
        speed = speedy;
    // Minimum flick speed (~600 CSS px/s) and a sane cap.
    if (speed < 600.0f)
        return false;
    if (vx > 8000.0f)
        vx = 8000.0f;
    else if (vx < -8000.0f)
        vx = -8000.0f;
    if (vy > 8000.0f)
        vy = 8000.0f;
    else if (vy < -8000.0f)
        vy = -8000.0f;
    // The finger was moving content with the drag (scroll += -finger);
    // on release the content keeps coasting the same direction.
    v->fling_vx = -vx;
    v->fling_vy = -vy;
    v->fling_fx = 0.0f;
    v->fling_fy = 0.0f;
    webview_clamp_scroll(v);
    if (v->fling_timer)
        aroma_timer_cancel(v->fling_timer);
    v->fling_timer = aroma_timer_create(16, true, webview_fling_cb, node);
    return v->fling_timer != NULL;
}

static bool webview_event_handler(AromaEvent *event, void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    if (!node || !node->node_widget_ptr || !event)
        return false;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;

    switch (event->event_type) {
    case EVENT_TYPE_MOUSE_CLICK: {
        if (v->active_pointer_id != -1 || v->mouse_down)
            return false;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        if (!webview_point_in(v, x, y))
            return false;
        aroma_ui_set_focused_node(node);
        v->mouse_down = true;
        v->dragging = false;
        v->mouse_x = x;
        v->mouse_y = y;
        v->press_scroll_x = v->scroll_x;
        v->press_scroll_y = v->scroll_y;
        return true;
    }
    case EVENT_TYPE_MOUSE_MOVE: {
        if (!v->mouse_down)
            return false;
        int dx = v->mouse_x - event->data.mouse.x;
        int dy = v->mouse_y - event->data.mouse.y;
        int slop = webview_dp(4);
        if (slop < 1)
            slop = 1;
        if (!v->dragging && (dx > slop || dx < -slop || dy > slop || dy < -slop))
            v->dragging = true;
        if (v->dragging) {
            // Physical drag delta -> CSS scroll delta so panning is dp-aware.
            float ds = webview_device_scale();
            int raw_dx = v->mouse_x - event->data.mouse.x;
            int raw_dy = v->mouse_y - event->data.mouse.y;
            int ddx, ddy;
            if (ds < 0.01f || ds == 1.0f) {
                ddx = raw_dx;
                ddy = raw_dy;
            } else {
                ddx = (int)((float)raw_dx / ds + (raw_dx >= 0 ? 0.5f : -0.5f));
                ddy = (int)((float)raw_dy / ds + (raw_dy >= 0 ? 0.5f : -0.5f));
            }
            v->scroll_x = v->press_scroll_x + ddx;
            v->scroll_y = v->press_scroll_y + ddy;
            webview_clamp_scroll(v);
            webview_request_render(node);
        }
        return true;
    }
    case EVENT_TYPE_MOUSE_RELEASE: {
        if (!v->mouse_down)
            return false;
        v->mouse_down = false;
        int x = event->data.mouse.x, y = event->data.mouse.y;
        if (!v->dragging && webview_point_in(v, x, y))
            webview_do_click(node, x, y);
        v->dragging = false;
        return webview_point_in(v, x, y);
    }
    case EVENT_TYPE_MOUSE_SCROLL: {
        int x = event->data.mouse.x, y = event->data.mouse.y;
        if (!webview_point_in(v, x, y))
            return false;
        webview_do_scroll(node, 0, (int)(event->data.mouse.scroll_y * -40));
        return true;
    }
    case EVENT_TYPE_TOUCH_DOWN: {
        if (v->active_pointer_id != -1)
            return false;
        int x = event->data.touch.x, y = event->data.touch.y;
        if (!webview_point_in(v, x, y))
            return false;
        aroma_ui_set_focused_node(node);
        v->active_pointer_id = event->data.touch.id;
        v->dragging = false;
        v->press_x = x;
        v->press_y = y;
        v->press_scroll_x = v->scroll_x;
        v->press_scroll_y = v->scroll_y;
        // A new gesture preempts any running fling.
        webview_fling_stop(v);
        webview_vel_reset(v);
        webview_vel_push(v, x, y);
        return true;
    }
    case EVENT_TYPE_TOUCH_MOVE: {
        if (v->active_pointer_id != event->data.touch.id)
            return false;
        int dx = v->press_x - event->data.touch.x;
        int dy = v->press_y - event->data.touch.y;
        int slop = webview_dp(6);
        if (slop < 1)
            slop = 1;
        if (!v->dragging && (dx > slop || dx < -slop || dy > slop || dy < -slop))
            v->dragging = true;
        if (v->dragging) {
            float ds = webview_device_scale();
            int ddx, ddy;
            if (ds < 0.01f || ds == 1.0f) {
                ddx = dx;
                ddy = dy;
            } else {
                ddx = (int)((float)dx / ds + (dx >= 0 ? 0.5f : -0.5f));
                ddy = (int)((float)dy / ds + (dy >= 0 ? 0.5f : -0.5f));
            }
            v->scroll_x = v->press_scroll_x + ddx;
            v->scroll_y = v->press_scroll_y + ddy;
            webview_clamp_scroll(v);
            webview_vel_push(v, event->data.touch.x, event->data.touch.y);
            webview_request_render(node);
        }
        return true;
    }
    case EVENT_TYPE_TOUCH_UP: {
        if (v->active_pointer_id != event->data.touch.id)
            return false;
        v->active_pointer_id = -1;
        int x = event->data.touch.x, y = event->data.touch.y;
        if (!v->dragging && webview_point_in(v, x, y))
            webview_do_click(node, x, y);
        else if (v->dragging)
            webview_fling_start(node, v, webview_device_scale());
        v->dragging = false;
        return webview_point_in(v, x, y);
    }
    case EVENT_TYPE_KEY_PRESS: {
#ifdef AROMA_HAS_WEBVIEW
        if (!v->page)
            return false;
        unsigned gen = aroma_webpage_nav_gen(v->page);
        if (aroma_webpage_key(v->page, event->data.key.key_code)) {
            webview_sync_after_page(node, gen);
            webview_request_render(node);
            return true;
        }
#endif
        return false;
    }
    case EVENT_TYPE_CUSTOM: {
#ifdef AROMA_HAS_WEBVIEW
        // UTF-8 text committed by the platform IME (see aroma_webview.h).
        if (event->data.custom.custom_type != AROMA_WEBVIEW_TEXT_INPUT_EVENT)
            return false;
        if (!v->page || !event->data.custom.data)
            return false;
        unsigned gen = aroma_webpage_nav_gen(v->page);
        if (aroma_webpage_text_input(v->page, (const char *)event->data.custom.data)) {
            webview_sync_after_page(node, gen);
            webview_request_render(node);
            aroma_event_consume(event);
            return true;
        }
#endif
        return false;
    }
    case EVENT_TYPE_FOCUS_LOST: {
        // Another widget took UI focus: drop the page input and the IME.
        webview_hide_keyboard(v);
#ifdef AROMA_HAS_WEBVIEW
        if (v->page)
            aroma_webpage_blur(v->page);
#endif
        return false;
    }
    default:
        return false;
    }
}

static void webview_ensure_events(AromaNode *node, AromaWebView *v)
{
    if (v->events_registered)
        return;
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, webview_event_handler, node, 80);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, webview_event_handler, node, 80);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_KEY_PRESS, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_CUSTOM, webview_event_handler, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_FOCUS_LOST, webview_event_handler, node, 90);
    v->events_registered = true;
}

static bool webview_upload(AromaNode *node, AromaWebView *v,
                           const unsigned char *pixels, int w, int h)
{
    if (!pixels || w != v->rect.width || h != v->rect.height || w <= 0 || h <= 0) {
        webview_diag(node, "no-pixels");
        return false;
    }
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return false;
    if (v->texture_id != 0 && (v->tex_w != w || v->tex_h != h)) {
        if (v->owns_texture && gfx->unload_image)
            gfx->unload_image(v->texture_id);
        v->texture_id = 0;
    }
    // Prefer the streaming upload path (no per-frame mipmap rebuild);
    // other backends fall back to the regular RGBA path.
    bool use_dynamic = (gfx->load_dynamic_image && gfx->update_dynamic_image);
    if (v->texture_id == 0) {
        if (use_dynamic) {
            v->texture_id = gfx->load_dynamic_image((unsigned char *)pixels, w, h);
        } else if (gfx->load_image_from_rgba) {
            v->texture_id = gfx->load_image_from_rgba((unsigned char *)pixels, w, h);
        }
        if (v->texture_id == 0) {
            webview_diag(node, "upload-create-fail");
            return false;
        }
        v->owns_texture = true;
        v->tex_w = w;
        v->tex_h = h;
    } else {
        bool ok = false;
        if (use_dynamic)
            ok = gfx->update_dynamic_image(v->texture_id, (unsigned char *)pixels, w, h);
        else if (gfx->update_image_rgba)
            ok = gfx->update_image_rgba(v->texture_id, (unsigned char *)pixels, w, h);
        if (!ok) {
            if (v->owns_texture && gfx->unload_image)
                gfx->unload_image(v->texture_id);
            v->texture_id = 0;
            v->owns_texture = false;
            webview_diag(node, "upload-update-fail");
            return false;
        }
    }
    return v->texture_id != 0;
}

// Consume a finished async frame when it matches the latest request:
// upload it and wake the widget for a repaint. Returns true when the fresh
// frame landed. Stale frames (superseded requests) and size mismatches are
// dropped; the pending render rests with the raster worker / timer.
static bool webview_try_land_async(AromaNode *node)
{
#ifdef AROMA_HAS_WEBVIEW
    if (!node || !node->node_widget_ptr)
        return false;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page)
        return false;
    int w = 0, h = 0;
    unsigned seq = 0;
    const unsigned char *px = aroma_webpage_poll_async(v->page, &w, &h, &seq);
    if (!px || seq != v->render_seq)
        return false;
    if (w != v->rect.width || h != v->rect.height) {
        // Page view diverged from the widget (density settle, rounding):
        // re-sync so the next worker frame matches. The frame is dropped;
        // the caller re-posts for the current sequence.
        aroma_webpage_resize(v->page, v->rect.width, v->rect.height);
        return false;
    }
    if (!webview_upload(node, v, px, w, h))
        return false;
    v->need_render = false;
    v->need_upload = false;
    aroma_node_invalidate(node);
    return true;
#else
    (void)node;
    return false;
#endif
}

static void webview_ensure_render(AromaNode *node)
{
#ifdef AROMA_HAS_WEBVIEW
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page || v->rect.width <= 0 || v->rect.height <= 0)
        return;
    // Refresh DPR when it genuinely changes (sharpness only). The widget
    // rect is physical px and is never mutated here: rescaling geometry by
    // a float ratio on transient density values wedged rect vs page size
    // (permanent white). Any divergence is healed by resize below.
    {
        float want = webview_device_scale();
        float have = aroma_webpage_get_device_scale(v->page);
        float diff = want > have ? want - have : have - want;
        if (diff > 0.001f) {
            aroma_webpage_set_device_scale(v->page, want);
            webview_clamp_scroll(v);
            v->render_seq++; // supersede any in-flight frame for the old scale
            v->need_render = true;
            v->need_upload = true;
        }
    }
    aroma_webpage_set_view_scroll(v->page, v->scroll_x, v->scroll_y);
    if (v->need_render) {
        webview_clamp_scroll(v);
        // A frame from a previous post may already be waiting: land it.
        if (!webview_try_land_async(node)) {
            // Preferred path: rasterize off the UI thread so scrolling
            // never blocks input, layout or compositing. The current
            // texture (if any) keeps showing until the fresh frame lands.
            if (aroma_webpage_render_async(v->page, v->scroll_x, v->scroll_y,
                                           v->render_seq)) {
                if (v->texture_id != 0 && v->tex_w == v->rect.width &&
                    v->tex_h == v->rect.height)
                    v->need_upload = false;
                // Try once now; otherwise the periodic JS timer tick lands
                // the frame (a draw only happens for invalidated nodes).
                webview_try_land_async(node);
                return;
            }
            int w = 0, h = 0;
            const unsigned char *px = aroma_webpage_render(v->page, v->scroll_x,
                                                           v->scroll_y, &w, &h);
            if (!px) {
                webview_diag(node, "render-null");
                return;
            }
            if (w != v->rect.width || h != v->rect.height) {
                // Page view diverged from the widget (density settle, rounding,
                // external layout). Re-sync and render once more instead of
                // wedging on white forever.
                aroma_webpage_resize(v->page, v->rect.width, v->rect.height);
                webview_clamp_scroll(v);
                px = aroma_webpage_render(v->page, v->scroll_x, v->scroll_y, &w, &h);
                if (!px || w != v->rect.width || h != v->rect.height) {
                    webview_diag(node, !px ? "render-null" : "render-size-mismatch");
                    return;
                }
            }
            if (webview_upload(node, v, px, w, h))
                v->need_render = false;
        }
    }
    if (v->need_upload) {
        int w = 0, h = 0;
        const unsigned char *pixels = aroma_webpage_pixels(v->page, &w, &h);
        if (webview_upload(node, v, pixels, w, h))
            v->need_upload = false;
    }
#else
    (void)node;
#endif
}

static void webview_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(node))
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (v->rect.width <= 0 || v->rect.height <= 0)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    webview_ensure_render(node);
    if (v->texture_id != 0 && !v->need_upload && gfx->draw_image) {
        gfx->draw_image(window_id, v->rect.x, v->rect.y, v->rect.width,
                        v->rect.height, v->texture_id, 0.0f);
        return;
    }
    if (gfx->fill_rectangle) {
        gfx->fill_rectangle(window_id, v->rect.x, v->rect.y, v->rect.width,
                            v->rect.height, 0xFFFFFFFF, false, 0.0f);
        if (gfx->draw_hollow_rectangle)
            gfx->draw_hollow_rectangle(window_id, v->rect.x, v->rect.y, v->rect.width,
                                       v->rect.height, 0xFF3A3A44, 1, false, 0.0f);
    }
}

static void webview_destroy_node(AromaNode *node)
{
    if (!node)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v)
        return;
    // Never leave the platform IME up for a widget that no longer exists.
    webview_hide_keyboard(v);
#ifdef AROMA_HAS_WEBVIEW
    if (v->js_timer) {
        aroma_timer_cancel(v->js_timer);
        v->js_timer = NULL;
    }
    webview_fling_stop(v);
#endif
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (v->texture_id != 0 && v->owns_texture && gfx && gfx->unload_image)
        gfx->unload_image(v->texture_id);
    v->texture_id = 0;
#ifdef AROMA_HAS_WEBVIEW
    if (v->page) {
        aroma_webpage_destroy(v->page);
        v->page = NULL;
    }
#endif
    free(v->last_html);
    v->last_html = NULL;
    aroma_widget_free(v);
    node->node_widget_ptr = NULL;
}

AromaNode *aroma_webview_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("aroma_webview: invalid parent or size");
        return NULL;
    }
    // Public geometry is authored in dp; store physical px on Android.
    x = webview_dp(x);
    y = webview_dp(y);
    width = webview_dp(width);
    height = webview_dp(height);
    AromaWebView *v = (AromaWebView *)aroma_widget_alloc(sizeof(AromaWebView));
    if (!v) {
        LOG_ERROR("aroma_webview: out of memory");
        return NULL;
    }
    memset(v, 0, sizeof(AromaWebView));
    v->rect.x = x;
    v->rect.y = y;
    v->rect.width = width;
    v->rect.height = height;
    v->active_pointer_id = -1;
    v->need_render = true;
    v->need_upload = true;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, v);
    if (!node) {
        aroma_widget_free(v);
        return NULL;
    }
    aroma_node_set_draw_cb(node, webview_draw);
    node->destroy_cb = webview_destroy_node;
#ifdef AROMA_HAS_WEBVIEW
    v->page = aroma_webpage_create(width, height);
    if (!v->page) {
        LOG_ERROR("aroma_webview: engine init failed");
        v->has_error = true;
        strncpy(v->error_msg, "web engine init failed", sizeof(v->error_msg) - 1);
    } else {
        // CSS px == dp on Android: render sharp at physical px.
        aroma_webpage_set_device_scale(v->page, webview_device_scale());
    }
#else
    v->has_error = true;
    strncpy(v->error_msg, "webview not supported on this platform", sizeof(v->error_msg) - 1);
#endif
    webview_ensure_events(node, v);
#ifdef AROMA_HAS_WEBVIEW
    if (v->page)
        v->js_timer = aroma_timer_create(30, true, webview_js_timer_cb, node);
#endif
    LOG_INFO("aroma_webview: created at (%d,%d) %dx%d", x, y, width, height);
    return node;
}

static void webview_remember(AromaWebView *v, const char *url, const char *html,
                             const char *base)
{
    if (url && url != v->last_url) {
        strncpy(v->last_url, url, sizeof(v->last_url) - 1);
        v->last_url[sizeof(v->last_url) - 1] = '\0';
    } else if (!url) {
        v->last_url[0] = '\0';
    }

    char *new_html = NULL;
    if (html) {
        new_html = (char *)malloc(strlen(html) + 1);
        if (new_html)
            strcpy(new_html, html);
    }
    free(v->last_html);
    v->last_html = new_html;
    if (base) {
        strncpy(v->last_base, base, sizeof(v->last_base) - 1);
        v->last_base[sizeof(v->last_base) - 1] = '\0';
    } else {
        v->last_base[0] = '\0';
    }
}

bool aroma_webview_load_url(AromaNode *webview_node, const char *url)
{
    if (!webview_node || !webview_node->node_widget_ptr || !url || !url[0])
        return false;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
#ifdef AROMA_HAS_WEBVIEW
    if (!v->page)
        return false;
    if (!aroma_webpage_load_url(v->page, url)) {
        webview_report_error(v, "failed to load page");
        if (v->on_error && v->error_msg[0])
            v->on_error(webview_node, v->error_msg, v->error_ud);
        aroma_node_invalidate(webview_node);
        return false;
    }
    webview_remember(v, url, NULL, NULL);
    webview_after_navigate(webview_node);
    return true;
#else
    (void)url;
    webview_report_error(v, "webview not supported on this platform");
    return false;
#endif
}

bool aroma_webview_load_html(AromaNode *webview_node, const char *html,
                             const char *base_url)
{
    if (!webview_node || !webview_node->node_widget_ptr || !html)
        return false;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
#ifdef AROMA_HAS_WEBVIEW
    if (!v->page)
        return false;
    if (!aroma_webpage_load_html(v->page, html, base_url ? base_url : "")) {
        webview_report_error(v, "failed to render page");
        if (v->on_error && v->error_msg[0])
            v->on_error(webview_node, v->error_msg, v->error_ud);
        aroma_node_invalidate(webview_node);
        return false;
    }
    webview_remember(v, NULL, html, base_url);
    webview_after_navigate(webview_node);
    return true;
#else
    (void)html;
    (void)base_url;
    webview_report_error(v, "webview not supported on this platform");
    return false;
#endif
}

bool aroma_webview_reload(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return false;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->last_url[0])
        return aroma_webview_load_url(webview_node, v->last_url);
    if (v->last_html)
        return aroma_webview_load_html(webview_node, v->last_html,
                                       v->last_base[0] ? v->last_base : NULL);
    webview_request_render(webview_node);
    return true;
}

bool aroma_webview_can_go_back(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return false;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    return v->page && aroma_webpage_can_go_back(v->page);
#else
    return false;
#endif
}

bool aroma_webview_can_go_forward(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return false;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    return v->page && aroma_webpage_can_go_forward(v->page);
#else
    return false;
#endif
}

bool aroma_webview_go_back(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return false;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (!v->page || !aroma_webpage_go_back(v->page))
        return false;
    webview_after_navigate(webview_node);
    return true;
#else
    return false;
#endif
}

bool aroma_webview_go_forward(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return false;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (!v->page || !aroma_webpage_go_forward(v->page))
        return false;
    webview_after_navigate(webview_node);
    return true;
#else
    return false;
#endif
}

const char *aroma_webview_get_url(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return NULL;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->page)
        return aroma_webpage_url(v->page);
#endif
    return NULL;
}

const char *aroma_webview_get_title(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return NULL;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->page)
        return aroma_webpage_title(v->page);
#endif
    return NULL;
}

void aroma_webview_set_text_scale(AromaNode *webview_node, float scale)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->page) {
        aroma_webpage_set_text_scale(v->page, scale);
        webview_request_render(webview_node);
    }
#else
    (void)scale;
#endif
}

float aroma_webview_get_text_scale(AromaNode *webview_node)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return 1.0f;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->page)
        return aroma_webpage_text_scale(v->page);
#endif
    return 1.0f;
}

void aroma_webview_scroll_to(AromaNode *webview_node, int x, int y)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->scroll_x = x;
    v->scroll_y = y;
    webview_clamp_scroll(v);
    webview_request_render(webview_node);
}

void aroma_webview_scroll_by(AromaNode *webview_node, int dx, int dy)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    if (!dx && !dy)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->scroll_x += dx;
    v->scroll_y += dy;
    webview_clamp_scroll(v);
    webview_request_render(webview_node);
}

void aroma_webview_get_scroll(AromaNode *webview_node, int *x, int *y)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (x)
        *x = v->scroll_x;
    if (y)
        *y = v->scroll_y;
}

void aroma_webview_get_content_size(AromaNode *webview_node, int *w, int *h)
{
    if (w)
        *w = 0;
    if (h)
        *h = 0;
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
#ifdef AROMA_HAS_WEBVIEW
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    if (v->page)
        aroma_webpage_content_size(v->page, w, h);
#endif
}

void aroma_webview_set_on_navigate(AromaNode *webview_node,
                                   AromaWebViewNavigateCb cb, void *user_data)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->on_navigate = cb;
    v->navigate_ud = user_data;
}

void aroma_webview_set_on_title(AromaNode *webview_node,
                               AromaWebViewTitleCb cb, void *user_data)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->on_title = cb;
    v->title_ud = user_data;
}

void aroma_webview_set_on_error(AromaNode *webview_node,
                               AromaWebViewErrorCb cb, void *user_data)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->on_error = cb;
    v->error_ud = user_data;
    if (cb && v->has_error && v->error_msg[0])
        cb(webview_node, v->error_msg, user_data);
}

void aroma_webview_set_on_load(AromaNode *webview_node,
                               AromaWebViewLoadCb cb, void *user_data)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->on_load = cb;
    v->load_ud = user_data;
}

const char *aroma_webview_eval_js(AromaNode *webview_node, const char *code)
{
    if (!webview_node || !webview_node->node_widget_ptr || !code)
        return "";
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
#ifdef AROMA_HAS_WEBVIEW
    if (!v->page)
        return "";
    unsigned gen = aroma_webpage_nav_gen(v->page);
    const char *r = aroma_webpage_eval(v->page, code);
    webview_sync_after_page(webview_node, gen);
    return r ? r : "";
#else
    return "";
#endif
}

static void webview_bridge_trampoline(const char *arg, char *out, size_t outlen, void *user)
{
    AromaWebBridgeEntry *bctx = (AromaWebBridgeEntry *)user;
    if (!bctx || !bctx->cb) {
        if (out && outlen > 0)
            out[0] = '\0';
        return;
    }
    bctx->cb(bctx->node, bctx->name, arg ? arg : "", out, outlen, bctx->user);
}

bool aroma_webview_add_js_bridge(AromaNode *webview_node, const char *name,
                                 AromaWebViewJsBridgeCb cb, void *user_data)
{
    if (!webview_node || !webview_node->node_widget_ptr || !name || !name[0] || !cb)
        return false;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
#ifdef AROMA_HAS_WEBVIEW
    if (!v->page)
        return false;
    if (v->bridge_count >= AROMA_WEBVIEW_MAX_BRIDGES)
        return false;
    for (int i = 0; i < v->bridge_count; i++) {
        if (!strcmp(v->bridges[i].name, name)) {
            v->bridges[i].node = webview_node;
            v->bridges[i].cb = cb;
            v->bridges[i].user = user_data;
            return aroma_webpage_add_bridge(v->page, name, webview_bridge_trampoline,
                                            &v->bridges[i]);
        }
    }
    int i = v->bridge_count++;
    v->bridges[i].node = webview_node;
    strncpy(v->bridges[i].name, name, sizeof(v->bridges[i].name) - 1);
    v->bridges[i].name[sizeof(v->bridges[i].name) - 1] = '\0';
    v->bridges[i].cb = cb;
    v->bridges[i].user = user_data;
    return aroma_webpage_add_bridge(v->page, name, webview_bridge_trampoline,
                                    &v->bridges[i]);
#else
    (void)cb;
    (void)user_data;
    return false;
#endif
}

void aroma_webview_set_size(AromaNode *webview_node, int width, int height)
{
    if (!webview_node || !webview_node->node_widget_ptr || width <= 0 || height <= 0)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    // dp in, px stored (matches create path).
    int px_w = webview_dp(width);
    int px_h = webview_dp(height);
    if (v->rect.width == px_w && v->rect.height == px_h)
        return;
    v->rect.width = px_w;
    v->rect.height = px_h;
#ifdef AROMA_HAS_WEBVIEW
    if (v->page) {
        aroma_webpage_set_device_scale(v->page, webview_device_scale());
        aroma_webpage_resize(v->page, px_w, px_h);
    }
#endif
    webview_clamp_scroll(v);
    webview_request_render(webview_node);
}

void aroma_webview_set_position(AromaNode *webview_node, int x, int y)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->rect.x = webview_dp(x);
    v->rect.y = webview_dp(y);
    aroma_node_invalidate(webview_node);
}
