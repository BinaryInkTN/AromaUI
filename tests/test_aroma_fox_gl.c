













#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "aroma_3d.h"
#include "core/aroma_fox_model.h"

#include <stdio.h>
#include <stdlib.h>

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
                         EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
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
    return 1;
}

static void egl_teardown(void)
{
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

int main(void)
{
    if (!egl_setup())
    {
        printf("SKIP: no EGL display available\n");
        return 0;
    }

    CHECK(aroma_3d_init(), "3d resources initialize");

    Aroma3DModel *fox =
        aroma_3d_load_model_from_memory(aroma_fox_glb, aroma_fox_glb_len);
    CHECK(fox != NULL, "fox loads from embedded header");
    if (!fox)
    {
        printf("fox_gl: passed=%d failed=%d\n", s_passed, s_failed);
        egl_teardown();
        return 1;
    }
    CHECK(aroma_3d_get_mesh_count(fox) == 1, "fox has one mesh");

    Aroma3DCamera cam;
    aroma_3d_camera_init(&cam);
    float bmin[3], bmax[3];
    aroma_3d_get_model_bounds(fox, bmin, bmax);
    cam.target[0] = (bmin[0] + bmax[0]) * 0.5f;
    cam.target[1] = (bmin[1] + bmax[1]) * 0.5f;
    cam.target[2] = (bmin[2] + bmax[2]) * 0.5f;
    float dx = bmax[0] - bmin[0];
    float dy = bmax[1] - bmin[1];
    float dz = bmax[2] - bmin[2];
    float max_dim = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    cam.radius = max_dim * 1.5f;

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    while (glGetError() != GL_NO_ERROR)
    {
    }

    CHECK(aroma_3d_render_to_rect(fox, &cam, 0, 0, WIN_W, WIN_H, WIN_W, WIN_H),
          "fox renders to rect");
    CHECK(glGetError() == GL_NO_ERROR, "render raises no GL error");

    unsigned char *px =
        (unsigned char *)malloc((size_t)WIN_W * WIN_H * 4);
    CHECK(px != NULL, "pixel buffer allocated");
    if (px)
    {
        glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        long lit = 0;
        long total_lum = 0;
        long orange = 0;
        for (int i = 0; i < WIN_W * WIN_H; i++)
        {
            int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
            int lum = (r + g + b) / 3;
            total_lum += lum;
            if (lum > 10)
            {
                lit++;
                if (r > g + 25 && r > b + 25)
                    orange++;
            }
        }
        double lit_frac = (double)lit / (WIN_W * WIN_H);
        double mean = (double)total_lum / (WIN_W * WIN_H);
        double orange_frac = lit ? (double)orange / lit : 0.0;
        printf("fox pixels: lit_frac=%.3f mean_lum=%.1f orange_frac=%.3f\n",
               lit_frac, mean, orange_frac);
        CHECK(lit_frac > 0.02 && lit_frac < 0.95, "fox covers a lit portion of frame");
        CHECK(mean > 3.0, "frame is not black");
        CHECK(orange_frac > 0.2, "fox texture applied (orange body visible)");
        free(px);
    }

    aroma_3d_destroy_model(fox);

    printf("fox_gl: passed=%d failed=%d\n", s_passed, s_failed);
    egl_teardown();
    return s_failed ? 1 : 0;
}
