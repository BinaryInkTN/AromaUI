#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "widgets/aroma_video.h"
#include "widgets/aroma_video_codec.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "aroma_timer.h"
#include "aroma_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef AROMA_VIDEO_FIXTURE_DIR
#define AROMA_VIDEO_FIXTURE_DIR "assets/video"
#endif

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

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void fill_frame(unsigned char *px, int w, int h,
                       unsigned char r, unsigned char g, unsigned char b)
{
    for (int i = 0; i < w * h; i++)
    {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
}

static void test_update_rgba_fast_path(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    static unsigned char red[32 * 16 * 4];
    static unsigned char green[32 * 16 * 4];
    static unsigned char wide[64 * 16 * 4];
    fill_frame(red, 32, 16, 255, 0, 0);
    fill_frame(green, 32, 16, 0, 255, 0);
    fill_frame(wide, 64, 16, 0, 0, 255);

    CHECK(!gfx->update_image_rgba(0, red, 32, 16), "update rejects id 0");
    CHECK(!gfx->update_image_rgba(999999, red, 32, 16), "update rejects bad id");
    CHECK(!gfx->update_image_rgba(1, NULL, 32, 16), "update rejects NULL pixels");

    unsigned int tex = gfx->load_image_from_rgba(red, 32, 16);
    CHECK(tex != 0, "rgba texture uploads");

    CHECK(gfx->update_image_rgba(tex, green, 32, 16), "same-size update succeeds");
    int tw = 0, th = 0;
    CHECK(gfx->get_image_size(tex, &tw, &th) && tw == 32 && th == 16,
          "size tracked after same-size update");

    CHECK(gfx->update_image_rgba(tex, wide, 64, 16), "resize update succeeds");
    CHECK(gfx->get_image_size(tex, &tw, &th) && tw == 64 && th == 16,
          "size tracked after resize");

    gfx->unload_image(tex);
}

static void test_push_frame_streaming(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *v = aroma_video_create(root, NULL, 10, 10, 120, 80);
    CHECK(v != NULL, "push video node created");

    static unsigned char f1[64 * 48 * 4];
    static unsigned char f2[64 * 48 * 4];
    static unsigned char f3[32 * 32 * 4];
    fill_frame(f1, 64, 48, 200, 30, 30);
    fill_frame(f2, 64, 48, 30, 200, 30);
    fill_frame(f3, 32, 32, 30, 30, 200);

    CHECK(aroma_video_push_frame(v, f1, 64, 48), "first push uploads");
    unsigned int t1 = aroma_video_get_texture_id(v);
    CHECK(t1 != 0, "texture exists after first push");
    CHECK(aroma_video_get_video_width(v) == 64, "video width tracked");
    CHECK(aroma_video_get_video_height(v) == 48, "video height tracked");
    CHECK(aroma_video_is_playing(v), "push starts playback");

    CHECK(aroma_video_push_frame(v, f2, 64, 48), "second push uploads");
    unsigned int t2 = aroma_video_get_texture_id(v);
    CHECK(t1 == t2, "same-size push reuses texture");

    CHECK(aroma_video_push_frame(v, f3, 32, 32), "resize push uploads");
    CHECK(aroma_video_get_video_width(v) == 32, "resized width tracked");

    gfx->clear(0, 0xFF202020);
    aroma_video_draw(v, 0);
    gfx->graphics_flush();
    CHECK(glGetError() == GL_NO_ERROR, "video draw leaves clean GL state");

    aroma_video_pause(v);
    CHECK(!aroma_video_is_playing(v), "pause works after push");

    __destroy_node(root);
    __node_system_destroy();
}

static void fixture_path(char *out, size_t len, const char *name)
{
    snprintf(out, len, "%s/%s", AROMA_VIDEO_FIXTURE_DIR, name);
}

static bool decode_fixture(const char *name, double seek_to)
{
    char path[1024];
    fixture_path(path, sizeof(path), name);
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *v = aroma_video_create(root, path, 0, 0, 160, 90);
    bool ok = (v != NULL);
    if (ok)
        ok = (aroma_video_get_state(v) == AROMA_VIDEO_STOPPED);
    if (ok) {
        double dur = aroma_video_get_duration(v);
        ok = (dur > 0.8 && dur < 1.5);
    }
    if (ok)
        ok = aroma_video_seek(v, seek_to);
    if (ok) {
        ok = (aroma_video_get_texture_id(v) != 0 &&
              aroma_video_get_video_width(v) == 160 &&
              aroma_video_get_video_height(v) == 90);
    }
    if (ok) {
        double pos = aroma_video_get_position(v);
        ok = (pos >= seek_to - 0.15 && pos <= seek_to + 0.15);
    }
    if (ok) {
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        gfx->clear(0, 0xFF202020);
        aroma_video_draw(v, 0);
        gfx->graphics_flush();
        ok = (glGetError() == GL_NO_ERROR);
    }
    __destroy_node(root);
    __node_system_destroy();
    return ok;
}

static void test_decode_all_formats(void)
{
    CHECK(decode_fixture("sample_h264.mp4", 0.5), "h264/mp4 decodes and seeks");
    CHECK(decode_fixture("sample_vp9.webm", 0.5), "vp9/webm decodes and seeks");
    CHECK(decode_fixture("sample_mpeg1.mpg", 0.5), "mpeg1/ps decodes and seeks");
    CHECK(decode_fixture("sample_mjpeg.avi", 0.5), "mjpeg/avi decodes and seeks");
}

static int s_alpha_frames = 0;
static int s_alpha_bad_frames = 0;

static void alpha_probe_cb(const unsigned char *rgba, int w, int h,
                           double pts, void *user)
{
    (void)pts;
    (void)user;
    s_alpha_frames++;
    for (int i = 3; i < w * h * 4; i += 4)
    {
        if (rgba[i] != 0xFF)
        {
            s_alpha_bad_frames++;
            break;
        }
    }
}

// pl_mpeg's frame->RGBA conversion only writes the RGB bytes (the alpha
// byte is left as uninitialized memory), which made web previews fully
// transparent. Every backend must deliver opaque frames.
static void test_decoded_frames_are_opaque(void)
{
    char path[1024];
    fixture_path(path, sizeof(path), "sample_mpeg1.mpg");

    char err[256] = { 0 };
    s_alpha_frames = 0;
    s_alpha_bad_frames = 0;
    AromaVideoCodec *c = aroma_video_codec_open_file(path, alpha_probe_cb, NULL,
                                                     err, sizeof(err));
    CHECK(c != NULL, "codec opens mpeg1 fixture for alpha probe");
    if (!c)
        return;

    printf("  alpha probe backend: %s\n", aroma_video_codec_backend(c));
    for (int i = 0; i < 40 && s_alpha_frames == 0; i++)
        aroma_video_codec_step(c, 0.1);

    CHECK(s_alpha_frames > 0, "codec delivers at least one decoded frame");
    CHECK(s_alpha_bad_frames == 0, "decoded RGBA frames are fully opaque");
    aroma_video_codec_close(c);
}

static void test_decode_memory_and_playback(void)
{
    char path[1024];
    fixture_path(path, sizeof(path), "sample_h264.mp4");
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "memory: fixture readable");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    CHECK(data != NULL, "memory: buffer allocated");
    if (!data) {
        fclose(f);
        return;
    }
    CHECK(fread(data, 1, (size_t)size, f) == (size_t)size, "memory: fixture loaded");
    fclose(f);

    __node_system_init();
    aroma_timer_init();
    AromaNode *root = make_root();
    AromaNode *v = aroma_video_create_from_memory(root, data, (size_t)size,
                                                 0, 0, 160, 90);
    free(data);
    CHECK(v != NULL, "memory: video created from mp4 bytes");
    if (!v) {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_STOPPED, "memory: starts stopped");
    double fps = aroma_video_get_framerate(v);
    CHECK(fps > 9.0 && fps < 11.0, "memory: framerate detected");

    aroma_video_play(v);
    CHECK(aroma_video_is_playing(v), "memory: playing after play");
    usleep(200 * 1000);
    aroma_timer_tick(aroma_time_now_ms());
    usleep(200 * 1000);
    aroma_timer_tick(aroma_time_now_ms());
    CHECK(aroma_video_get_texture_id(v) != 0, "memory: frames decoded during playback");
    CHECK(aroma_video_get_position(v) > 0.0, "memory: position advances");

    aroma_video_pause(v);
    double held = aroma_video_get_position(v);
    usleep(120 * 1000);
    aroma_timer_tick(aroma_time_now_ms());
    CHECK(aroma_video_get_position(v) == held, "memory: position holds while paused");

    aroma_video_stop(v);
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_STOPPED, "memory: stopped after stop");

    __destroy_node(root);
    __node_system_destroy();
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

static void test_osd_renders(void)
{
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __node_system_init();
    AromaNode *root = make_root();

    // 240x180 at (8,8): OSD bar spans y 142..188, play center (31,165).
    AromaNode *v = aroma_video_create(root, NULL, 8, 8, 240, 180);
    CHECK(v != NULL, "osd node created");
    if (!v)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    static unsigned char frame[64 * 48 * 4];
    fill_frame(frame, 64, 48, 200, 30, 30);
    CHECK(aroma_video_push_frame(v, frame, 64, 48), "osd frame uploads");
    aroma_video_pause(v);

    gfx->clear(0, 0xFF202020);
    aroma_video_draw(v, 0);
    gfx->graphics_flush();
    CHECK(glGetError() == GL_NO_ERROR, "osd draw leaves clean GL state");

    unsigned char *buf = (unsigned char *)malloc(WIN_W * WIN_H * 4);
    CHECK(buf != NULL, "osd pixel buffer allocated");
    if (!buf)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    read_pixels(buf);
    int r, g, b;
    // Scrim over the red frame, right of the seek track end.
    px_rgb(buf, 244, 175, &r, &g, &b);
    CHECK(r < 170 && r > 40, "osd scrim darkens frame");
    // Knob at track start (fraction 0 in push mode).
    px_rgb(buf, 52, 165, &r, &g, &b);
    CHECK(r > 200 && g > 200 && b > 200, "osd knob paints white");
    // Frame content above the bar stays pure red.
    px_rgb(buf, 120, 60, &r, &g, &b);
    CHECK(r > 190 && g < 60, "osd leaves content visible");
    free(buf);

    aroma_video_set_show_controls(v, false);
    gfx->clear(0, 0xFF202020);
    aroma_video_draw(v, 0);
    gfx->graphics_flush();
    CHECK(glGetError() == GL_NO_ERROR, "osd hidden draws clean");

    __destroy_node(root);
    __node_system_destroy();
}

int main(void)
{
    printf("=== Aroma Video GL Tests ===\n");
    if (!egl_setup())
    {
        printf("SKIP: no EGL display available\n");
        return 0;
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->load_image_from_rgba || !gfx->update_image_rgba)
    {
        printf("SKIP: backend lacks streaming hooks\n");
        egl_teardown();
        return 0;
    }
    if (gfx->setup_shared_window_resources)
        gfx->setup_shared_window_resources();
    if (gfx->setup_separate_window_resources)
        gfx->setup_separate_window_resources(0);

    test_update_rgba_fast_path();
    test_push_frame_streaming();
    test_decode_all_formats();
    test_decoded_frames_are_opaque();
    test_decode_memory_and_playback();
    test_osd_renders();

    egl_teardown();
    printf("Video GL: %d passed, %d failed\n", s_passed, s_failed);
    return s_failed > 0 ? 1 : 0;
}
