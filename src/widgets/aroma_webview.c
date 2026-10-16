#include "widgets/aroma_webview.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_dp.h"
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
    bool events_registered;
} AromaWebView;

#ifdef __ANDROID__
static inline int webview_dp(int dp) { return aroma_android_dp_to_px(dp); }
#else
static inline int webview_dp(int dp) { return dp; }
#endif

static void webview_request_render(AromaNode *node);
static void webview_after_navigate(AromaNode *node);

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
        int mx = cw - v->rect.width;
        int my = ch - v->rect.height;
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
    v->need_render = true;
    v->need_upload = true;
    aroma_node_invalidate(node);
}

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
}
#endif

static void webview_after_navigate(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
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

static void webview_do_click(AromaNode *node, int x, int y)
{
#ifdef AROMA_HAS_WEBVIEW
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page || !webview_point_in(v, x, y))
        return;
    unsigned gen = aroma_webpage_nav_gen(v->page);
    int cx = x - v->rect.x + v->scroll_x;
    int cy = y - v->rect.y + v->scroll_y;
    if (aroma_webpage_click(v->page, cx, cy))
        webview_after_navigate(node);
    else
        webview_sync_after_page(node, gen);
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
        if (!v->dragging && (dx > 4 || dx < -4 || dy > 4 || dy < -4))
            v->dragging = true;
        if (v->dragging) {
            v->scroll_x = v->press_scroll_x + (v->mouse_x - event->data.mouse.x);
            v->scroll_y = v->press_scroll_y + (v->mouse_y - event->data.mouse.y);
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
        return true;
    }
    case EVENT_TYPE_TOUCH_MOVE: {
        if (v->active_pointer_id != event->data.touch.id)
            return false;
        int dx = v->press_x - event->data.touch.x;
        int dy = v->press_y - event->data.touch.y;
        if (!v->dragging && (dx > 6 || dx < -6 || dy > 6 || dy < -6))
            v->dragging = true;
        if (v->dragging) {
            v->scroll_x = v->press_scroll_x + dx;
            v->scroll_y = v->press_scroll_y + dy;
            webview_clamp_scroll(v);
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
    v->events_registered = true;
}

static void webview_ensure_render(AromaNode *node)
{
#ifdef AROMA_HAS_WEBVIEW
    if (!node || !node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)node->node_widget_ptr;
    if (!v->page || v->rect.width <= 0 || v->rect.height <= 0)
        return;
    aroma_webpage_set_view_scroll(v->page, v->scroll_x, v->scroll_y);
    if (v->need_render) {
        webview_clamp_scroll(v);
        int w = 0, h = 0;
        const unsigned char *px = aroma_webpage_render(v->page, v->scroll_x,
                                                       v->scroll_y, &w, &h);
        if (!px || w != v->rect.width || h != v->rect.height) {

            return;
        }
        v->need_render = false;
        v->need_upload = true;
    }
    if (v->need_upload) {
        int w = 0, h = 0;
        const unsigned char *pixels = aroma_webpage_pixels(v->page, &w, &h);
        if (!pixels || w != v->rect.width || h != v->rect.height || w <= 0 || h <= 0)
            return;
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (!gfx)
            return;
        if (v->texture_id != 0 && (v->tex_w != w || v->tex_h != h)) {
            if (v->owns_texture && gfx->unload_image)
                gfx->unload_image(v->texture_id);
            v->texture_id = 0;
        }
        if (v->texture_id == 0) {
            if (gfx->load_image_from_rgba) {
                v->texture_id = gfx->load_image_from_rgba((unsigned char *)pixels, w, h);
                v->owns_texture = (v->texture_id != 0);
                v->tex_w = w;
                v->tex_h = h;
            }
        } else if (gfx->update_image_rgba) {
            if (!gfx->update_image_rgba(v->texture_id, (unsigned char *)pixels, w, h)) {
                if (v->owns_texture && gfx->unload_image)
                    gfx->unload_image(v->texture_id);
                v->texture_id = 0;
                v->owns_texture = false;
                return;
            }
        } else {
            return;
        }
        if (v->texture_id != 0)
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
#ifdef AROMA_HAS_WEBVIEW
    if (v->js_timer) {
        aroma_timer_cancel(v->js_timer);
        v->js_timer = NULL;
    }
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
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
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
    if (v->rect.width == width && v->rect.height == height)
        return;
    v->rect.width = width;
    v->rect.height = height;
#ifdef AROMA_HAS_WEBVIEW
    if (v->page)
        aroma_webpage_resize(v->page, width, height);
#endif
    webview_clamp_scroll(v);
    webview_request_render(webview_node);
}

void aroma_webview_set_position(AromaNode *webview_node, int x, int y)
{
    if (!webview_node || !webview_node->node_widget_ptr)
        return;
    AromaWebView *v = (AromaWebView *)webview_node->node_widget_ptr;
    v->rect.x = x;
    v->rect.y = y;
    aroma_node_invalidate(webview_node);
}
