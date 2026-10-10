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

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);


    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    AromaNode *main_container = IncenseFindWidget(g_registry, "main_container");
    aroma_node_set_gap(main_container, 5);

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
