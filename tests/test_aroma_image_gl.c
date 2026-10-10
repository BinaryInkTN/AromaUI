
















#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "widgets/aroma_image.h"
#include "aroma_font.h"
#include "aroma_ubuntu_font.h"
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

static void read_pixels(unsigned char *out)
{
    glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, out);
}


static void px_rgba(const unsigned char *buf, int ux, int uy,
                    int *r, int *g, int *b, int *a)
{
    int row = WIN_H - 1 - uy;
    const unsigned char *p = buf + (row * WIN_W + ux) * 4;
    if (r)
        *r = p[0];
    if (g)
        *g = p[1];
    if (b)
        *b = p[2];
    if (a)
        *a = p[3];
}

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}



static unsigned int make_split_texture(AromaGraphicsInterface *gfx)
{
    static unsigned char data[100 * 50 * 4];
    for (int y = 0; y < 50; y++)
        for (int x = 0; x < 100; x++)
        {
            unsigned char *p = data + (y * 100 + x) * 4;
            if (x < 25)
            {
                p[0] = 255;
                p[1] = 0;
                p[2] = 0;
            }
            else
            {
                p[0] = 0;
                p[1] = 0;
                p[2] = 255;
            }
            p[3] = 255;
        }
    return gfx->load_image_from_rgba(data, 100, 50);
}

static void test_cover_crops_center(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();
    unsigned int tex = make_split_texture(gfx);
    CHECK(tex != 0, "cover: split texture uploads");

    int tw = 0, th = 0;
    CHECK(gfx->get_image_size && gfx->get_image_size(tex, &tw, &th) &&
              tw == 100 && th == 50,
          "cover: get_image_size reports 100x50");




    AromaNode *img = aroma_image_create_from_texture(root, tex, 10, 10, 60, 40, true);
    CHECK(img != NULL, "cover: image node created");
    aroma_image_set_scale_mode(img, AROMA_IMAGE_SCALE_COVER);
    gfx->clear(0, 0xFF202020);
    aroma_image_draw(img, 0);
    gfx->graphics_flush();

    unsigned char *buf = malloc(WIN_W * WIN_H * 4);
    read_pixels(buf);
    int r, b;
    px_rgba(buf, 22, 20, &r, NULL, &b, NULL);
    CHECK(b > 200 && r < 50, "cover: cropped boundary position (blue)");
    px_rgba(buf, 14, 20, &r, NULL, &b, NULL);
    CHECK(r > 200 && b < 50, "cover: left region stays red");
    free(buf);
    aroma_image_destroy(img);
    __node_system_destroy();
}

static void test_fill_stretches(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();
    unsigned int tex = make_split_texture(gfx);



    AromaNode *img = aroma_image_create_from_texture(root, tex, 10, 10, 60, 40, true);
    aroma_image_set_scale_mode(img, AROMA_IMAGE_SCALE_FILL);
    gfx->clear(0, 0xFF202020);
    aroma_image_draw(img, 0);
    gfx->graphics_flush();

    unsigned char *buf = malloc(WIN_W * WIN_H * 4);
    read_pixels(buf);
    int r, b;
    px_rgba(buf, 22, 20, &r, NULL, &b, NULL);
    CHECK(r > 200 && b < 50, "fill: stretched boundary position (red)");
    free(buf);
    aroma_image_destroy(img);
    __node_system_destroy();
}

static void test_rounded_circle_over_cover(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();





    static unsigned char data[50 * 100 * 4];
    for (int i = 0; i < 50 * 100; i++)
    {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 0;
        data[i * 4 + 2] = 255;
        data[i * 4 + 3] = 255;
    }
    unsigned int tex = gfx->load_image_from_rgba(data, 50, 100);
    CHECK(tex != 0, "round: portrait texture uploads");

    AromaNode *img = aroma_image_create_from_texture(root, tex, 10, 10, 40, 40, true);
    aroma_image_set_scale_mode(img, AROMA_IMAGE_SCALE_COVER);
    aroma_image_set_corner_radius(img, 20.0f);
    gfx->clear(0, 0xFF202020);
    aroma_image_draw(img, 0);
    gfx->graphics_flush();

    unsigned char *buf = malloc(WIN_W * WIN_H * 4);
    read_pixels(buf);
    int r, b;
    px_rgba(buf, 14, 14, &r, &b, NULL, NULL);
    CHECK(r < 100 && b < 100, "round: corner outside circle shows backdrop");
    px_rgba(buf, 30, 30, &r, NULL, &b, NULL);
    CHECK(b > 200, "round: circle center shows image");
    px_rgba(buf, 30, 12, &r, NULL, &b, NULL);
    CHECK(b > 200, "round: top edge middle shows image");
    free(buf);
    aroma_image_destroy(img);
    __node_system_destroy();
}




static void test_lazy_nonscii_glyph_keeps_text(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                    aroma_ubuntu_ttf_len, 16);
    CHECK(font != NULL, "lazy glyph: test font loads");
    if (!font)
        return;

    gfx->clear(0, 0xFFFFFFFF);
    gfx->render_text(0, font, "X \xc2\xb7 Y", 20, 40, 0xFF1C1B1F, 1.0f);
    gfx->graphics_flush();

    unsigned char *buf = malloc(WIN_W * WIN_H * 4);
    read_pixels(buf);
    int dark = 0;
    for (int y = 30; y < 60; y++)
        for (int x = 10; x < 120; x++)
        {
            int r, g, b;
            px_rgba(buf, x, y, &r, &g, &b, NULL);
            if (r < 100 && g < 100 && b < 100)
                dark++;
        }
    CHECK(dark > 20, "lazy glyph: mixed-ascii string leaves ink");
    free(buf);
    aroma_font_destroy(font);
}

int main(void)
{
    printf("=== Aroma Image Scale/Round GL Tests ===\n");
    if (!egl_setup())
    {
        printf("SKIP: no EGL display available\n");
        return 0;
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->draw_image_uv || !gfx->get_image_size)
    {
        printf("SKIP: backend lacks image scale hooks\n");
        egl_teardown();
        return 0;
    }
    if (gfx->setup_shared_window_resources)
        gfx->setup_shared_window_resources();
    if (gfx->setup_separate_window_resources)
        gfx->setup_separate_window_resources(0);

    test_cover_crops_center();
    test_fill_stretches();
    test_rounded_circle_over_cover();
    test_lazy_nonscii_glyph_keeps_text();

    egl_teardown();
    printf("Image Scale GL: %d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
