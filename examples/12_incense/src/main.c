#include <aroma.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;

static void flip_details(void *userdata)
{
    (void)userdata;
    bool shown = false;
    IncenseStateGetBool("details", &shown);
    IncenseStateSetBool("details", !shown);
}

static void flip_extra(void *userdata)
{
    (void)userdata;
    bool shown = false;
    IncenseStateGetBool("extra", &shown);
    IncenseStateSetBool("extra", !shown);
}

static void flip_beta(void *userdata)
{
    (void)userdata;
    bool shown = false;
    IncenseStateGetBool("beta", &shown);
    IncenseStateSetBool("beta", !shown);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseStateSetString("welcome", "State drives this screen");
    IncenseStateSetBool("details", false);
    IncenseStateSetBool("extra", true);
    IncenseStateSetInt("level", 65);
    IncenseStateSetFloat("flevel", 0.65f);
    IncenseStateSetBool("beta", false);
    IncenseRegisterCallback("flip_details", INCENSE_CALLBACK_VOID_PTR, flip_details, NULL);
    IncenseRegisterCallback("flip_extra", INCENSE_CALLBACK_VOID_PTR, flip_extra, NULL);
    IncenseRegisterCallback("flip_beta", INCENSE_CALLBACK_VOID_PTR, flip_beta, NULL);
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
