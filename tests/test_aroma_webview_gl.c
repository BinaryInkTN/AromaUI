#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "widgets/aroma_webview.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 256
#define WIN_H 256

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLSurface s_surface = EGL_NO_SURFACE;
static EGLContext s_context = EGL_NO_CONTEXT;

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static void stub_make_current(size_t window_id)
{
    (void)window_id;
    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
}

static void stub_get_size(size_t window_id, int *w, int *h)
{
    (void)window_id;
    if (w)
        *w = WIN_W;
    if (h)
        *h = WIN_H;
}

static AromaPlatformInterface stub_platform;

static AromaPlatformInterface *stub_get_platform(void)
{
    return &stub_platform;
}

static int egl_setup(void)
{
    typedef EGLDisplay (*GetPlatformDisplayFn)(EGLenum, void *, const EGLint *);
    GetPlatformDisplayFn get_platform =
        (GetPlatformDisplayFn)eglGetProcAddress("eglGetPlatformDisplay");
    if (get_platform)
        s_display = get_platform(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
    if (s_display == EGL_NO_DISPLAY)
        s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_display == EGL_NO_DISPLAY)
        return 0;

    EGLint maj, min;
    if (!eglInitialize(s_display, &maj, &min))
        return 0;

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint nc = 0;
    if (!eglChooseConfig(s_display, cfg_attr, &cfg, 1, &nc) || nc == 0)
        return 0;

    EGLint pb[] = {EGL_WIDTH, WIN_W, EGL_HEIGHT, WIN_H, EGL_NONE};
    s_surface = eglCreatePbufferSurface(s_display, cfg, pb);
    if (s_surface == EGL_NO_SURFACE)
        return 0;

    EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    s_context = eglCreateContext(s_display, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (s_context == EGL_NO_CONTEXT)
        return 0;

    if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context))
        return 0;

    memset(&stub_platform, 0, sizeof(stub_platform));
    stub_platform.make_context_current = stub_make_current;
    stub_platform.get_window_size = stub_get_size;
    aroma_backend_abi.get_platform_interface = stub_get_platform;
    return 1;
}

static void egl_teardown(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->shutdown)
        gfx->shutdown();
    if (s_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_context);
        if (s_surface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
    }
}

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void read_pixels(unsigned char *out)
{
    glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

static void px_rgb(const unsigned char *buf, int ux, int uy,
                   int *r, int *g, int *b)
{
    int row = WIN_H - 1 - uy;
    const unsigned char *p = buf + (row * WIN_W + ux) * 4;
    if (r)
        *r = p[0];
    if (g)
        *g = p[1];
    if (b)
        *b = p[2];
}

static const char *kPage =
    "<html><head><title>GL</title></head>"
    "<body style=\"margin:0;background:#ffffff;\">"
    "<div style=\"width:100px;height:60px;background:#ff0000;\">hi</div>"
    "<div style=\"height:400px;background:#00ff00;\">tall</div>"
    "</body></html>";

static void test_render_pixels(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *wv = aroma_webview_create(root, 10, 10, 200, 200);
    CHECK(wv != NULL, "gl node created");
    if (!wv) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_webview_load_html(wv, kPage, NULL), "gl page loads");

    gfx->clear(0, 0xFF202020);

    unsigned char *buf = (unsigned char *)malloc(WIN_W * WIN_H * 4);
    CHECK(buf != NULL, "gl pixel buffer allocated");
    if (!buf) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    /* Pump frames so the texture uploads and draws. */
    for (int i = 0; i < 4; i++) {
        gfx->clear(0, 0xFF202020);
        AromaNodeDrawFn draw = aroma_node_get_draw_cb(wv);
        if (draw)
            draw(wv, 0);
        gfx->graphics_flush();
    }
    read_pixels(buf);
    int r, g, b;
    /* Red box at page (0..100, 0..60) -> window (10..110, 10..70). */
    px_rgb(buf, 10 + 50, 10 + 30, &r, &g, &b);
    CHECK(r > 200 && g < 80 && b < 80, "gl red box renders");
    /* White page background below the red box. */
    px_rgb(buf, 10 + 150, 10 + 30, &r, &g, &b);
    CHECK(r > 200 && g > 200 && b > 200, "gl page background renders");
    /* Green spacer further down is scrolled out of view. */
    px_rgb(buf, 10 + 50, 10 + 150, &r, &g, &b);
    CHECK(g > 200 && r < 80, "gl green spacer visible below");

    /* Scroll down: red box leaves the viewport. */
    aroma_webview_scroll_to(wv, 0, 100);
    for (int i = 0; i < 4; i++) {
        gfx->clear(0, 0xFF202020);
        AromaNodeDrawFn draw = aroma_node_get_draw_cb(wv);
        if (draw)
            draw(wv, 0);
        gfx->graphics_flush();
    }
    read_pixels(buf);
    px_rgb(buf, 10 + 50, 10 + 30, &r, &g, &b);
    CHECK(!(r > 200 && g < 80 && b < 80), "gl red box scrolled away");

    /* JavaScript restyles the box: set blue via the DOM, redraw, verify. */
    aroma_webview_scroll_to(wv, 0, 0);
    const char *res = aroma_webview_eval_js(
        wv, "document.querySelector('div').style.setProperty('background', '#0000ff'); 'ok'");
    CHECK(res && res[0] == 'o', "gl js restyle evaluates");
    for (int i = 0; i < 4; i++) {
        gfx->clear(0, 0xFF202020);
        AromaNodeDrawFn draw = aroma_node_get_draw_cb(wv);
        if (draw)
            draw(wv, 0);
        gfx->graphics_flush();
    }
    read_pixels(buf);
    px_rgb(buf, 10 + 50, 10 + 30, &r, &g, &b);
    CHECK(b > 200 && r < 80, "gl js restyle paints blue");

    free(buf);
    __destroy_node(root);
    __node_system_destroy();
}

int main(void)
{
    printf("=== Aroma WebView GL Tests ===\n");
    if (!egl_setup()) {
        printf("SKIP: no EGL display available\n");
        return 0;
    }
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->draw_image || !gfx->load_image_from_rgba) {
        printf("SKIP: backend lacks image hooks\n");
        egl_teardown();
        return 0;
    }
    if (gfx->setup_shared_window_resources)
        gfx->setup_shared_window_resources();
    if (gfx->setup_separate_window_resources)
        gfx->setup_separate_window_resources(0);

    test_render_pixels();

    egl_teardown();
    printf("WebView GL: %d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
