#ifdef __EMSCRIPTEN__
#include "aroma_platform_interface.h"
#include <emscripten/html5.h>
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"

typedef struct {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
    int canvas_width;
    int canvas_height;
    double device_pixel_ratio;
    void (*frame_callback)(size_t, void *);
    void *frame_callback_data;
    double last_mouse_x;
    double last_mouse_y;
    bool mouse_button_down;
    bool initialized;
    int active_touch_id;
} AromaEmscriptenContext;

static AromaEmscriptenContext platform_ctx = {0};

static int              g_last_type   = -1;
static int              g_last_x      = 0;
static int              g_last_y      = 0;
static unsigned long long g_last_target = 0ULL;

EM_JS(double, _aroma_get_device_pixel_ratio, (void), {
    return window.devicePixelRatio || 1.0;
});

EM_JS(void, _aroma_resize_canvas_for_dpr, (const char *selector, int css_w, int css_h, double dpr), {
    var sel = UTF8ToString(selector);
    var el  = document.querySelector(sel);
    if (!el) return;
    el.width  = Math.round(css_w  * dpr);
    el.height = Math.round(css_h * dpr);
    el.style.width  = css_w  + 'px';
    el.style.height = css_h + 'px';
});

EM_JS(void, _js_expose_mouse, (int type, int target, int x, int y, int btn), {
    if (typeof window !== 'undefined') {
        window.aromaLastMouseType   = type;
        window.aromaLastMouseTarget = target;
        window.aromaLastMouseX      = x;
        window.aromaLastMouseY      = y;
        window.aromaLastMouseButton = btn;
    }
});

EMSCRIPTEN_KEEPALIVE int               aroma_test_get_last_mouse_event_type(void)    { return g_last_type;   }
EMSCRIPTEN_KEEPALIVE int               aroma_test_get_last_mouse_event_x(void)       { return g_last_x;      }
EMSCRIPTEN_KEEPALIVE int               aroma_test_get_last_mouse_event_y(void)       { return g_last_y;      }
EMSCRIPTEN_KEEPALIVE unsigned long long aroma_test_get_last_mouse_event_target(void) { return g_last_target; }

static inline void _client_to_canvas(double cx, double cy,
                                     double *out_x, double *out_y)
{
    double css_w = 0.0, css_h = 0.0;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);

    double dpr = platform_ctx.device_pixel_ratio;
    if (dpr < 1.0) dpr = 1.0;

    double physical_w = css_w * dpr;
    double physical_h = css_h * dpr;

    double sx = (css_w > 0.0) ? (physical_w / css_w) : 1.0;
    double sy = (css_h > 0.0) ? (physical_h / css_h) : 1.0;

    *out_x = cx * sx;
    *out_y = cy * sy;
}

static bool _queue_mouse_event(AromaEventType type,
                               double mx, double my,
                               uint8_t button)
{
    AromaNode *root = aroma_event_get_root();
    if (!root) return false;

    AromaNode *target    = aroma_event_hit_test(root, (int)mx, (int)my);
    uint64_t   target_id = target ? target->node_id : root->node_id;

    AromaEvent *ev = aroma_event_create_mouse(type, target_id,
                                              (int)mx, (int)my, button);
    if (!ev) {
        LOG_ERROR("_queue_mouse_event: alloc failed type=%d x=%.1f y=%.1f", type, mx, my);
        return false;
    }

    ev->data.mouse.delta_x = (int)(mx - platform_ctx.last_mouse_x);
    ev->data.mouse.delta_y = (int)(my - platform_ctx.last_mouse_y);

    bool queued = aroma_event_queue(ev);
    if (queued) {
        g_last_type   = (int)type;
        g_last_x      = (int)mx;
        g_last_y      = (int)my;
        g_last_target = (unsigned long long)target_id;
        _js_expose_mouse(g_last_type, (int)g_last_target,
                         g_last_x, g_last_y, (int)button);
    }

    LOG_INFO("mouse_event: type=%d target=%llu x=%.1f y=%.1f btn=%d d=(%d,%d) ok=%d",
             type, (unsigned long long)target_id, mx, my, button,
             ev->data.mouse.delta_x, ev->data.mouse.delta_y, queued);
    return queued;
}

EM_JS(void, _aroma_emscripten_dispatch_mouse_js, (int action, int x, int y, int button), {
    if (typeof window !== 'undefined') {
        window.aromaLastMouseDispatchAction = action;
        window.aromaLastMouseDispatchX      = x;
        window.aromaLastMouseDispatchY      = y;
        window.aromaLastMouseDispatchButton = button;
    }
    console.log('dispatch_mouse', action, x, y, button);
});

EMSCRIPTEN_KEEPALIVE
void aroma_emscripten_dispatch_mouse(int action, int x, int y, int button)
{
    LOG_INFO("dispatch_mouse: action=%d x=%d y=%d btn=%d", action, x, y, button);

    _aroma_emscripten_dispatch_mouse_js(action, x, y, button);

    double dpr = platform_ctx.device_pixel_ratio;
    if (dpr < 1.0) dpr = 1.0;
    double dx = (double)x * dpr;
    double dy = (double)y * dpr;

    switch (action) {
    case 0:
        platform_ctx.last_mouse_x = dx;
        platform_ctx.last_mouse_y = dy;
        _queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, dx, dy, (uint8_t)button);
        aroma_event_handle_pointer_move((int)dx, (int)dy, platform_ctx.mouse_button_down);
        break;

    case 1:
        platform_ctx.mouse_button_down = true;
        platform_ctx.last_mouse_x = dx;
        platform_ctx.last_mouse_y = dy;
        _queue_mouse_event(EVENT_TYPE_MOUSE_CLICK, dx, dy, (uint8_t)button);
        aroma_event_handle_pointer_move((int)dx, (int)dy, true);
        break;

    case 2:
        platform_ctx.mouse_button_down = false;
        platform_ctx.last_mouse_x = dx;
        platform_ctx.last_mouse_y = dy;
        _queue_mouse_event(EVENT_TYPE_MOUSE_RELEASE, dx, dy, (uint8_t)button);
        aroma_event_handle_pointer_move((int)dx, (int)dy, false);
        break;

    default:
        LOG_WARNING("dispatch_mouse: unknown action %d", action);
        break;
    }
}

static bool _queue_key_event(AromaEventType type,
                             uint32_t key_value,
                             uint16_t modifiers)
{
    AromaNode *root = aroma_event_get_root();
    if (!root) return false;

    AromaNode *target = aroma_ui_get_focused_node();
    if (!target) target = root;

    AromaEvent *ev = aroma_event_create_key(type, target->node_id,
                                            key_value, modifiers);
    if (!ev) return false;

    return aroma_event_queue(ev);
}

static uint32_t _map_key(const EmscriptenKeyboardEvent *e)
{
    if (e->key[0] != '\0' && e->key[1] == '\0')
        return (uint32_t)(unsigned char)e->key[0];

    static const struct { const char *name; uint32_t val; } kmap[] = {
        { "Backspace",  8      },
        { "Tab",        9      },
        { "Enter",      10     },
        { "Escape",     27     },
        { "Delete",     127    },
        { "ArrowLeft",  0xFF51 },
        { "ArrowUp",    0xFF52 },
        { "ArrowRight", 0xFF53 },
        { "ArrowDown",  0xFF54 },
        { "Home",       0xFF50 },
        { "End",        0xFF57 },
        { "PageUp",     0xFF55 },
        { "PageDown",   0xFF56 },
        { "F1",         0xFFBE },
        { "F2",         0xFFBF },
        { "F3",         0xFFC0 },
        { "F4",         0xFFC1 },
        { "F5",         0xFFC2 },
        { "F6",         0xFFC3 },
        { "F7",         0xFFC4 },
        { "F8",         0xFFC5 },
        { "F9",         0xFFC6 },
        { "F10",        0xFFC7 },
        { "F11",        0xFFC8 },
        { "F12",        0xFFC9 },
    };

    for (size_t i = 0; i < sizeof(kmap)/sizeof(kmap[0]); ++i)
        if (strcmp(e->key, kmap[i].name) == 0)
            return kmap[i].val;

    return 0;
}

static inline uint16_t _key_modifiers(const EmscriptenKeyboardEvent *e)
{
    uint16_t m = 0;
    if (e->ctrlKey)  m |= AROMA_KEY_MOD_CTRL;
    if (e->shiftKey) m |= AROMA_KEY_MOD_SHIFT;
    if (e->altKey)   m |= AROMA_KEY_MOD_ALT;
    return m;
}

static EM_BOOL _cb_mouse_move(int et, const EmscriptenMouseEvent *e, void *ud)
{
    (void)et; (void)ud;
    double cx, cy;
    _client_to_canvas(e->targetX, e->targetY, &cx, &cy);

    if (cx == platform_ctx.last_mouse_x && cy == platform_ctx.last_mouse_y)
        return EM_TRUE;

    _queue_mouse_event(EVENT_TYPE_MOUSE_MOVE, cx, cy, 0);
    aroma_event_handle_pointer_move((int)cx, (int)cy,
                                    platform_ctx.mouse_button_down);
    platform_ctx.last_mouse_x = cx;
    platform_ctx.last_mouse_y = cy;
    return EM_TRUE;
}

static EM_BOOL _cb_mouse_down(int et, const EmscriptenMouseEvent *e, void *ud)
{
    (void)et; (void)ud;
    double cx, cy;
    _client_to_canvas(e->targetX, e->targetY, &cx, &cy);

    platform_ctx.mouse_button_down = true;
    platform_ctx.last_mouse_x = cx;
    platform_ctx.last_mouse_y = cy;

    bool ok = _queue_mouse_event(EVENT_TYPE_MOUSE_CLICK, cx, cy,
                                 (uint8_t)e->button);
    LOG_INFO("mouse_down: target=(%.1f,%.1f) canvas=(%.1f,%.1f) btn=%d ok=%d",
             (double)e->targetX, (double)e->targetY, cx, cy, e->button, ok);
    return EM_TRUE;
}

static EM_BOOL _cb_mouse_up(int et, const EmscriptenMouseEvent *e, void *ud)
{
    (void)et; (void)ud;
    double cx, cy;
    _client_to_canvas(e->targetX, e->targetY, &cx, &cy);

    platform_ctx.mouse_button_down = false;
    platform_ctx.last_mouse_x = cx;
    platform_ctx.last_mouse_y = cy;

    bool ok = _queue_mouse_event(EVENT_TYPE_MOUSE_RELEASE, cx, cy,
                                 (uint8_t)e->button);
    LOG_INFO("mouse_up: target=(%.1f,%.1f) canvas=(%.1f,%.1f) btn=%d ok=%d",
             (double)e->targetX, (double)e->targetY, cx, cy, e->button, ok);
    aroma_event_handle_pointer_move((int)cx, (int)cy, false);
    return EM_TRUE;
}

static EM_BOOL _cb_wheel(int et, const EmscriptenWheelEvent *e, void *ud)
{
    (void)et; (void)ud;
    int mx = (int)platform_ctx.last_mouse_x;
    int my = (int)platform_ctx.last_mouse_y;

    AromaNode *root = aroma_event_get_root();
    if (!root) return EM_TRUE;

    AromaNode *target = aroma_event_hit_test(root, mx, my);
    uint64_t   nid    = target ? target->node_id : root->node_id;

    double dpr = platform_ctx.device_pixel_ratio;
    if (dpr < 1.0) dpr = 1.0;

    AromaEvent *ev = aroma_event_create_scroll(nid, mx, my,
                                               (float)(e->deltaX * dpr),
                                               (float)(e->deltaY * dpr));
    if (ev) aroma_event_queue(ev);
    return EM_TRUE;
}

static EM_BOOL _cb_touch_start(int et, const EmscriptenTouchEvent *e, void *ud)
{
    (void)et; (void)ud;
    if (!e || e->numTouches <= 0 || !e->touches) return EM_TRUE;
    const EmscriptenTouchPoint *tp = &e->touches[0];
    double cx, cy;
    _client_to_canvas(tp->targetX, tp->targetY, &cx, &cy);
    platform_ctx.active_touch_id = tp->identifier;
    aroma_event_handle_touch(tp->identifier, (int)cx, (int)cy, 1);
    return EM_TRUE;
}

static EM_BOOL _cb_touch_move(int et, const EmscriptenTouchEvent *e, void *ud)
{
    (void)et; (void)ud;
    if (!e || e->numTouches <= 0 || !e->touches) return EM_TRUE;
    const EmscriptenTouchPoint *tp = &e->touches[0];
    double cx, cy;
    _client_to_canvas(tp->targetX, tp->targetY, &cx, &cy);
    platform_ctx.active_touch_id = tp->identifier;
    aroma_event_handle_touch(tp->identifier, (int)cx, (int)cy, 2);
    return EM_TRUE;
}

static EM_BOOL _cb_touch_end(int et, const EmscriptenTouchEvent *e, void *ud)
{
    (void)et; (void)ud;
    if (!e || e->numTouches <= 0 || !e->touches) return EM_TRUE;
    const EmscriptenTouchPoint *tp = &e->touches[0];
    double cx, cy;
    _client_to_canvas(tp->targetX, tp->targetY, &cx, &cy);
    aroma_event_handle_touch(tp->identifier, (int)cx, (int)cy, 0);
    platform_ctx.active_touch_id = -1;
    return EM_TRUE;
}

static EM_BOOL _cb_touch_cancel(int et, const EmscriptenTouchEvent *e, void *ud)
{
    (void)et; (void)ud;
    if (!e || e->numTouches <= 0 || !e->touches) return EM_TRUE;
    const EmscriptenTouchPoint *tp = &e->touches[0];
    aroma_event_handle_touch(tp->identifier, -1, -1, 0);
    platform_ctx.active_touch_id = -1;
    return EM_TRUE;
}

static EM_BOOL _cb_key_down(int et, const EmscriptenKeyboardEvent *e, void *ud)
{
    (void)et; (void)ud;
    uint32_t kv = _map_key(e);
    if (kv == 0) return EM_TRUE;
    _queue_key_event(EVENT_TYPE_KEY_PRESS, kv, _key_modifiers(e));
    return EM_TRUE;
}

static EM_BOOL _cb_key_up(int et, const EmscriptenKeyboardEvent *e, void *ud)
{
    (void)et; (void)ud;
    uint32_t kv = _map_key(e);
    if (kv == 0) return EM_TRUE;
    _queue_key_event(EVENT_TYPE_KEY_RELEASE, kv, _key_modifiers(e));
    return EM_TRUE;
}

static void _frame_trampoline(void *arg)
{
    (void)arg;
    if (platform_ctx.frame_callback) {
        // Emscripten backend currently supports a single implicit window (id 1)
        platform_ctx.frame_callback(1, platform_ctx.frame_callback_data);
    }
}

static int initialize(void)
{
    if (platform_ctx.initialized) {
        LOG_WARNING("initialize: already initialized");
        return 1;
    }

    platform_ctx.device_pixel_ratio = _aroma_get_device_pixel_ratio();
    if (platform_ctx.device_pixel_ratio < 1.0)
        platform_ctx.device_pixel_ratio = 1.0;

    double css_w = 640.0, css_h = 480.0;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    if (css_w <= 0.0 || css_h <= 0.0) {
        LOG_WARNING("initialize: canvas CSS size invalid (%.0fx%.0f), using 640x480",
                    css_w, css_h);
        css_w = 640.0; css_h = 480.0;
    }

    _aroma_resize_canvas_for_dpr("#canvas", (int)css_w, (int)css_h,
                                 platform_ctx.device_pixel_ratio);

    platform_ctx.canvas_width  = (int)(css_w  * platform_ctx.device_pixel_ratio);
    platform_ctx.canvas_height = (int)(css_h * platform_ctx.device_pixel_ratio);

    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.alpha      = EM_TRUE;
    attr.depth      = EM_TRUE;
    attr.stencil    = EM_FALSE;
    attr.antialias  = EM_FALSE;
    attr.majorVersion = 2;
    attr.minorVersion = 0;

    LOG_INFO("initialize: attempting WebGL context canvas_css=%.0fx%.0f dpr=%.2f attr={alpha=%d depth=%d stencil=%d antialias=%d ver=%d.%d}",
             css_w, css_h, platform_ctx.device_pixel_ratio,
             attr.alpha, attr.depth, attr.stencil, attr.antialias,
             attr.majorVersion, attr.minorVersion);

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        emscripten_webgl_create_context("#canvas", &attr);
    LOG_INFO("initialize: WebGL2 create_context returned %ld", (long)ctx);

    if (ctx <= 0) {
        LOG_WARNING("initialize: WebGL2 unavailable, falling back to WebGL1");
        attr.majorVersion = 1;
        ctx = emscripten_webgl_create_context("#canvas", &attr);
        LOG_INFO("initialize: WebGL1 create_context returned %ld", (long)ctx);
    }

    if (ctx <= 0) {
        LOG_CRITICAL("initialize: failed to create WebGL context");
        return 0;
    }

    platform_ctx.ctx = ctx;
    emscripten_webgl_make_context_current(ctx);

    emscripten_set_mousemove_callback("#canvas", NULL, EM_TRUE, _cb_mouse_move);
    emscripten_set_mousedown_callback("#canvas", NULL, EM_TRUE, _cb_mouse_down);
    emscripten_set_mouseup_callback  ("#canvas", NULL, EM_TRUE, _cb_mouse_up);
    emscripten_set_wheel_callback    ("#canvas", NULL, EM_TRUE, _cb_wheel);
    emscripten_set_touchstart_callback("#canvas", NULL, EM_TRUE, _cb_touch_start);
    emscripten_set_touchmove_callback("#canvas", NULL, EM_TRUE, _cb_touch_move);
    emscripten_set_touchend_callback  ("#canvas", NULL, EM_TRUE, _cb_touch_end);
    emscripten_set_touchcancel_callback("#canvas", NULL, EM_TRUE, _cb_touch_cancel);
    emscripten_set_keydown_callback  ("#canvas", NULL, EM_TRUE, _cb_key_down);
    emscripten_set_keyup_callback    ("#canvas", NULL, EM_TRUE, _cb_key_up);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->setup_shared_window_resources)
        gfx->setup_shared_window_resources();

    aroma_ui_set_immediate_mode(true);

    emscripten_set_main_loop_arg(_frame_trampoline, NULL, 0, 0);

    platform_ctx.initialized = true;
    LOG_INFO("initialize: ok — canvas=%dx%d (css=%.0fx%.0f dpr=%.2f) WebGL%d",
             platform_ctx.canvas_width, platform_ctx.canvas_height,
             css_w, css_h, platform_ctx.device_pixel_ratio,
             attr.majorVersion);
    return 1;
}

static size_t create_window(const char *title, int x, int y,
                            int width, int height)
{
    (void)title; (void)x; (void)y;

    double dpr = platform_ctx.device_pixel_ratio;
    if (dpr < 1.0) dpr = 1.0;

    int css_w = (width  > 0) ? width  : (int)(platform_ctx.canvas_width  / dpr);
    int css_h = (height > 0) ? height : (int)(platform_ctx.canvas_height / dpr);

    _aroma_resize_canvas_for_dpr("#canvas", css_w, css_h, dpr);

    platform_ctx.canvas_width  = (int)(css_w * dpr);
    platform_ctx.canvas_height = (int)(css_h * dpr);

    if (platform_ctx.ctx)
        emscripten_webgl_make_context_current(platform_ctx.ctx);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->setup_separate_window_resources)
        gfx->setup_separate_window_resources(1);

    LOG_INFO("create_window: css=%dx%d physical=%dx%d dpr=%.2f",
             css_w, css_h,
             platform_ctx.canvas_width, platform_ctx.canvas_height, dpr);
    return 1;
}

static void make_context_current(size_t window_id)
{
    (void)window_id;
    if (platform_ctx.ctx)
        emscripten_webgl_make_context_current(platform_ctx.ctx);
}

static void get_window_size(size_t window_id, int *w, int *h)
{
    (void)window_id;
    if (w) *w = platform_ctx.canvas_width;
    if (h) *h = platform_ctx.canvas_height;
}

static void set_window_update_callback(void (*cb)(size_t, void *), void *data)
{
    platform_ctx.frame_callback      = cb;
    platform_ctx.frame_callback_data = data;
}

static void request_window_update(size_t window_id)
{
    if (platform_ctx.frame_callback)
        platform_ctx.frame_callback(window_id, platform_ctx.frame_callback_data);
}

static bool run_event_loop(void)
{
    return true;
}

static void swap_buffers(size_t window_id)
{
    (void)window_id;
}

static void shutdown(void)
{
    if (platform_ctx.ctx) {
        emscripten_cancel_main_loop();
        emscripten_webgl_destroy_context(platform_ctx.ctx);
        platform_ctx.ctx         = 0;
        platform_ctx.initialized = false;
    }
}

static void *get_native_window_ptr(size_t window_id)  { (void)window_id; return NULL; }
static void *get_native_display_ptr(void)              { return NULL; }

AromaPlatformInterface aroma_platform_emscripten = {
    .initialize                     = initialize,
    .create_window                  = create_window,
    .make_context_current           = make_context_current,
    .get_window_size                = get_window_size,
    .set_window_update_callback     = set_window_update_callback,
    .request_window_update          = request_window_update,
    .run_event_loop                 = run_event_loop,
    .swap_buffers                   = swap_buffers,
    .shutdown                       = shutdown,
    .set_android_app                = NULL,
    .create_vulkan_surface          = NULL,
    .get_vulkan_instance_extensions = NULL,
    .get_native_window_ptr          = get_native_window_ptr,
    .get_native_display_ptr         = get_native_display_ptr,
};

#endif