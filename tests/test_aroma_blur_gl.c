/* Headless functional tests for frosted-glass backdrop blur (GLES3).
 *
 * Renders real frames on an EGL pbuffer (Mesa software GL is fine) and
 * reads pixels back to verify:
 *   1. blur works on the very first frame (snapshot capture path),
 *   2. the blur is smooth (no harsh banding),
 *   3. stacked glass shares one backdrop snapshot (upper glass does not
 *      re-blur the glass below it).
 *
 * Prints SKIP and exits 0 when no EGL display is available, so constrained
 * CI machines still pass.
 */

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* --- minimal platform stub: the GLES3 backend only needs context + size. */
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
    /* EGL_PLATFORM_SURFACELESS_MESA = 0x31DD */
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
    if (s_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (s_context != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_context);
        if (s_surface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
    }
}

/* Sharp black/white backdrop with a vertical edge at x=128. */
static void draw_edge_backdrop(AromaGraphicsInterface *gfx)
{
    gfx->clear(0, 0xFF202020);
    gfx->fill_rectangle(0, 0, 0, 128, WIN_H, 0xFFFFFFFF, false, 0.0f);
    gfx->fill_rectangle(0, 128, 0, 128, WIN_H, 0xFF000000, false, 0.0f);
}

static void read_pixels(unsigned char *out)
{
    glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

/* UI (top-left origin) -> teardown buffer index (GL bottom-up). */
static unsigned char px_at(const unsigned char *buf, int ux, int uy)
{
    int row = WIN_H - 1 - uy;
    return buf[(row * WIN_W + ux) * 4];
}

static void test_first_frame_blur(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    draw_edge_backdrop(gfx);
    /* Blur straddling the edge, issued on the very first rendered frame. */
    gfx->blur_backdrop(0, 96, 64, 64, 128, 16.0f, 0.0f);
    gfx->graphics_flush();

    unsigned char *buf = malloc(WIN_W * WIN_H * 4);
    read_pixels(buf);

    int far_left = px_at(buf, 100, 128);
    int far_right = px_at(buf, 156, 128);
    int edge = px_at(buf, 128, 128);
    CHECK(far_left > 240, "first-frame: far from edge stays white");
    CHECK(far_right < 15, "first-frame: far from edge stays black");
    CHECK(edge > 40 && edge < 215, "first-frame: edge pixel blended");

    /* Smoothness: no harsh steps across the transition. */
    int max_step = 0;
    int prev = px_at(buf, 108, 128);
    for (int x = 109; x <= 148; x++)
    {
        int cur = px_at(buf, x, 128);
        int step = abs(cur - prev);
        if (step > max_step)
            max_step = step;
        prev = cur;
    }
    CHECK(max_step < 110, "first-frame: blur gradient is smooth");
    free(buf);
}

static void read_region(const unsigned char *buf, int x, int y, int w, int h,
                        unsigned char *out)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            out[j * w + i] = px_at(buf, x + i, y + j);
}

static void test_stacked_glass_shares_backdrop(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    unsigned char ref[64 * 128];
    unsigned char stacked[64 * 128];
    unsigned char *buf = malloc(WIN_W * WIN_H * 4);

    /* Reference: single blur over a pristine backdrop. */
    draw_edge_backdrop(gfx);
    gfx->blur_backdrop(0, 96, 64, 64, 128, 16.0f, 0.0f);
    gfx->graphics_flush();
    read_pixels(buf);
    read_region(buf, 96, 64, 64, 128, ref);

    /* Stacked: a lower glass pass first, then the same region again. With a
     * shared per-frame snapshot both sample the identical backdrop, so the
     * second pass must match the reference (no double-blur darkening). */
    draw_edge_backdrop(gfx);
    gfx->blur_backdrop(0, 64, 64, 64, 128, 16.0f, 0.0f);
    gfx->blur_backdrop(0, 96, 64, 64, 128, 16.0f, 0.0f);
    gfx->graphics_flush();
    read_pixels(buf);
    read_region(buf, 96, 64, 64, 128, stacked);

    int max_diff = 0;
    for (int i = 0; i < 64 * 128; i += 4)
    {
        int d = abs((int)stacked[i] - (int)ref[i]);
        if (d > max_diff)
            max_diff = d;
    }
    CHECK(max_diff <= 8, "stacked glass shares one backdrop blur");
    free(buf);
}

int main(void)
{
    printf("=== Aroma Backdrop-Blur GL Tests ===\n");
    if (!egl_setup())
    {
        printf("SKIP: no EGL display available\n");
        return 0;
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->blur_backdrop)
    {
        printf("SKIP: backend has no backdrop blur\n");
        egl_teardown();
        return 0;
    }
    if (gfx->setup_shared_window_resources)
        gfx->setup_shared_window_resources();
    if (gfx->setup_separate_window_resources)
        gfx->setup_separate_window_resources(0);

    test_first_frame_blur();
    test_stacked_glass_shares_backdrop();

    egl_teardown();
    printf("Backdrop-Blur GL: %d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
