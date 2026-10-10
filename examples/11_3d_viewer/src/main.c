#include <aroma.h>
#include <aroma_3d.h>
#include <aroma_incense_loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;
static bool g_spinning = true;

static void note(const char *text)
{
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, text);
}

static void toggle_spin(void *userdata)
{
    (void)userdata;
    g_spinning = !g_spinning;
    AromaNode *cube = IncenseFindWidget(g_registry, "cube");
    AromaNode *fox = IncenseFindWidget(g_registry, "fox");
    if (cube)
        aroma_3d_viewer_set_auto_rotate(cube, g_spinning);
    if (fox)
        aroma_3d_viewer_set_auto_rotate(fox, g_spinning);
    note(g_spinning ? "Auto-rotate: on" : "Auto-rotate: off");
}

static void reset_cam(void *userdata)
{
    (void)userdata;
    AromaNode *cube = IncenseFindWidget(g_registry, "cube");
    AromaNode *fox = IncenseFindWidget(g_registry, "fox");
    if (cube)
        aroma_3d_viewer_reset_camera(cube);
    if (fox)
        aroma_3d_viewer_reset_camera(fox);
    note("Cameras reset");
}

static void set_view(void *userdata)
{
    int top = (int)(intptr_t)userdata;
    Aroma3DCamera cam = {0};
    cam.theta = top ? 0.0f : 0.9f;
    cam.phi = top ? 0.05f : 1.1f;
    cam.radius = 6.0f;
    cam.target[0] = 0.0f;
    cam.target[1] = 0.0f;
    cam.target[2] = 0.0f;
    cam.fov = 45.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;
    AromaNode *cube = IncenseFindWidget(g_registry, "cube");
    AromaNode *fox = IncenseFindWidget(g_registry, "fox");
    if (cube)
        aroma_3d_viewer_set_camera(cube, &cam);
    if (fox)
        aroma_3d_viewer_set_camera(fox, &cam);
    note(top ? "View: top" : "View: orbit");
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("toggle_spin", INCENSE_CALLBACK_VOID_PTR, toggle_spin, NULL);
    IncenseRegisterCallback("reset_cam", INCENSE_CALLBACK_VOID_PTR, reset_cam, NULL);
    IncenseRegisterCallback("view_orbit", INCENSE_CALLBACK_VOID_PTR, set_view, (void *)(intptr_t)0);
    IncenseRegisterCallback("view_top", INCENSE_CALLBACK_VOID_PTR, set_view, (void *)(intptr_t)1);
    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    if (!window)
    {
        int count = 0;
        const IncenseError *errors = IncenseGetErrors(&count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
        aroma_font_destroy(g_font);
        aroma_font_destroy(g_icon_font);
        aroma_ui_shutdown();
        return 1;
    }
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
    IncenseFreeRegistry(g_registry);
    aroma_font_destroy(g_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
#endif
