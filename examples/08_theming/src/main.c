#include <aroma.h>
#include <aroma_incense_loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;

static void apply(int which)
{
    const char *name = "high contrast";
    AromaTheme theme = aroma_theme_create_high_contrast();
    switch (which)
    {
    case 0: name = "light blue"; theme = aroma_theme_create_material_blue(); break;
    case 1: name = "light teal"; theme = aroma_theme_create_material_teal(); break;
    case 2: name = "light green"; theme = aroma_theme_create_material_green(); break;
    case 3: name = "light orange"; theme = aroma_theme_create_material_orange(); break;
    case 4: name = "light pink"; theme = aroma_theme_create_material_pink(); break;
    case 5: name = "dark blue"; theme = aroma_theme_create_material_blue_dark(); break;
    case 6: name = "dark teal"; theme = aroma_theme_create_material_teal_dark(); break;
    case 7: name = "dark green"; theme = aroma_theme_create_material_green_dark(); break;
    case 8: name = "dark orange"; theme = aroma_theme_create_material_orange_dark(); break;
    case 9: name = "dark pink"; theme = aroma_theme_create_material_pink_dark(); break;
    default: break;
    }
    aroma_ui_set_theme(&theme);
    char buf[64];
    snprintf(buf, sizeof(buf), "Theme: %s", name);
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, buf);
}

static void picked(int index, const char *option, void *userdata)
{
    (void)option;
    (void)userdata;
    apply(index);
}

static void surprise(void *userdata)
{
    (void)userdata;
    apply(rand() % 11);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("picked", INCENSE_CALLBACK_INT_STRING_PTR, picked, NULL);
    IncenseRegisterCallback("random", INCENSE_CALLBACK_VOID_PTR, surprise, NULL);
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
