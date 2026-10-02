/*
 * Headless functional tests for the procedural cube (GLES3).
 *
 * Renders `aroma_3d_create_cube()` on an EGL pbuffer (Mesa software GL
 * is fine) and reads pixels back to verify:
 *   1. the cube builds with 1 indexed mesh,
 *   2. the frame renders lit, non-white pixels (guards the zeroed
 *      model-matrix regression where every vertex collapsed and the
 *      cube rasterized nothing, plus the missing indexed-flag
 *      regression where the EBO was ignored),
 *   3. no GL error is raised while rendering.
 *
 * Prints SKIP and exits 0 when no EGL display is available, so constrained
 * CI machines still pass.
 */

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "aroma_3d.h"

#include <stdio.h>
#include <stdlib.h>

#define WIN_W 280
#define WIN_H 232

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

    Aroma3DModel *cube = aroma_3d_create_cube();
    CHECK(cube != NULL, "cube created");
    if (!cube)
    {
        printf("cube_gl: passed=%d failed=%d\n", s_passed, s_failed);
        egl_teardown();
        return 1;
    }
    CHECK(aroma_3d_get_mesh_count(cube) == 1, "cube has one mesh");

    /* Same viewpoint as the live-preview Cube tab. */
    Aroma3DCamera cam;
    aroma_3d_camera_init(&cam);
    cam.theta = 0.6f;
    cam.phi = 1.1f;
    cam.radius = 4.0f;
    cam.target[0] = 0.0f;
    cam.target[1] = 0.0f;
    cam.target[2] = 0.0f;

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    while (glGetError() != GL_NO_ERROR)
    {
    }

    CHECK(aroma_3d_render_to_rect(cube, &cam, 0, 0, WIN_W, WIN_H, WIN_W, WIN_H),
          "cube renders to rect");
    CHECK(glGetError() == GL_NO_ERROR, "render raises no GL error");

    unsigned char *px =
        (unsigned char *)malloc((size_t)WIN_W * WIN_H * 4);
    CHECK(px != NULL, "pixel buffer allocated");
    if (px)
    {
        glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        long nonwhite = 0;
        long blue = 0;
        for (int i = 0; i < WIN_W * WIN_H; i++)
        {
            int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
            if (r < 250 || g < 250 || b < 250)
                nonwhite++;
            if (b > 150 && b > r + 20 && g > r)
                blue++;
        }
        double frac = (double)nonwhite / (WIN_W * WIN_H);
        printf("cube pixels: nonwhite_frac=%.4f blue=%ld\n", frac, blue);
        CHECK(frac > 0.05, "cube covers a visible portion of frame");
        CHECK(blue > 100, "cube blue faces visible");

        /* Top-down and bottom-up views: guards the top/bottom face
           quads (each must be coplanar on y=+/-0.5 with matching
           normals, or its view renders empty). */
        struct
        {
            float phi;
            const char *name;
        } poles[] = {{0.25f, "top-down"}, {2.9f, "bottom-up"}};
        for (int v = 0; v < 2; v++)
        {
            cam.phi = poles[v].phi;
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (!aroma_3d_render_to_rect(cube, &cam, 0, 0, WIN_W, WIN_H, WIN_W, WIN_H))
            {
                printf("[FAIL] %s view renders (%s:%d)\n", poles[v].name, __FILE__, __LINE__);
                s_failed++;
                continue;
            }
            glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
            long lit = 0;
            for (int i = 0; i < WIN_W * WIN_H; i++)
            {
                int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
                if (r < 250 || g < 250 || b < 250)
                    lit++;
            }
            double lf = (double)lit / (WIN_W * WIN_H);
            printf("cube %s: nonwhite_frac=%.4f\n", poles[v].name, lf);
            if (lf > 0.05)
            {
                s_passed++;
            }
            else
            {
                printf("[FAIL] %s face visible (%s:%d)\n", poles[v].name, __FILE__, __LINE__);
                s_failed++;
            }
        }
        free(px);
    }

    aroma_3d_destroy_model(cube);

    printf("cube_gl: passed=%d failed=%d\n", s_passed, s_failed);
    egl_teardown();
    return s_failed ? 1 : 0;
}
