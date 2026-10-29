
#include <aroma.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "loud_dashboard.h"

#define LOUD_FONT_PX 20

static const int s_text_sizes[] = {12, 14, 16, 18, 20, 23, 24, 26, 40, 52};
static const int s_icon_sizes[] = {12, 14, 16, 18, 20, 24, 26, 32, 36, 40, 48, 64};
static AromaFont *s_text_fonts[sizeof(s_text_sizes) / sizeof(s_text_sizes[0])];
static AromaFont *s_icon_fonts[sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0])];

static AromaFont *s_font;
static AromaFont *s_icon_font;
static IncenseRegistry *s_registry;

static AromaTheme loud_theme(void)
{
    AromaTheme t = aroma_theme_create_material_black();
    t.colors.primary        = 0xFF723CEAu;
    t.colors.primary_dark   = 0xFF54309Au;
    t.colors.primary_light  = 0xFF9261F1u;
    t.colors.secondary      = 0xFFD4FD63u;
    t.colors.background     = 0xFF000000u;
    t.colors.surface        = 0xFF1A1A1Cu;
    t.colors.text_primary   = 0xFFFFFFFFu;
    t.colors.text_secondary = 0xFF9A93ADu;
    t.colors.border         = 0xFF332E45u;
    t.colors.error          = 0xFFFF375Fu;
    return t;
}

static void create_fonts(void)
{
    s_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                           aroma_ubuntu_ttf_len, LOUD_FONT_PX);
    s_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len,
                                                LOUD_FONT_PX);

    for (size_t i = 0; i < sizeof(s_text_sizes) / sizeof(s_text_sizes[0]); i++) {
        s_text_fonts[i] = aroma_font_create_from_memory(
            aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, s_text_sizes[i]);
        IncenseRegisterFontSize(s_text_sizes[i], s_text_fonts[i]);
    }
    for (size_t i = 0; i < sizeof(s_icon_sizes) / sizeof(s_icon_sizes[0]); i++) {
        s_icon_fonts[i] = aroma_font_create_from_memory(
            icon_ttf, icon_ttf_len, s_icon_sizes[i]);
        IncenseRegisterIconFontSize(s_icon_sizes[i], s_icon_fonts[i]);
    }

    loud_dashboard_bind_fonts(s_text_fonts, s_text_sizes,
                              (int)(sizeof(s_text_sizes) / sizeof(s_text_sizes[0])));
}

static void destroy_fonts(void)
{
    for (size_t i = 0; i < sizeof(s_text_fonts) / sizeof(s_text_fonts[0]); i++) {
        if (s_text_fonts[i])
            aroma_font_destroy(s_text_fonts[i]);
    }
    for (size_t i = 0; i < sizeof(s_icon_fonts) / sizeof(s_icon_fonts[0]); i++) {
        if (s_icon_fonts[i])
            aroma_font_destroy(s_icon_fonts[i]);
    }
    if (s_font)
        aroma_font_destroy(s_font);
    if (s_icon_font)
        aroma_font_destroy(s_icon_font);
}

static void print_load_errors(void)
{
    int count = 0;
    const IncenseError *errors = IncenseGetErrors(&count);
    for (int i = 0; i < count; i++)
        fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = loud_theme();
    aroma_ui_set_theme(&theme);

    create_fonts();
    loud_dashboard_register_callbacks();

    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", s_font,
                                            s_icon_font, &s_registry);
    if (!window) {
        print_load_errors();
        destroy_fonts();
        aroma_ui_shutdown();
        return 1;
    }

    loud_dashboard_after_mount(s_registry);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    IncenseFreeRegistry(s_registry);
    destroy_fonts();
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
