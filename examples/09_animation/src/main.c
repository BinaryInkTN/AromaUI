#include <aroma.h>
#include <aroma_animation.h>
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
static bool g_faded;
static bool g_wide;

static void slide_right(void *userdata)
{
    (void)userdata;
    AromaNode *box = IncenseFindWidget(g_registry, "box");
    if (!box)
        return;
    AromaAnimation *anim = aroma_animation_start(box, AROMA_ANIM_SLIDE_X, 16.0f, 110.0f, 600);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static void slide_back(void *userdata)
{
    (void)userdata;
    AromaNode *box = IncenseFindWidget(g_registry, "box");
    if (!box)
        return;
    AromaAnimation *anim = aroma_animation_start(box, AROMA_ANIM_SLIDE_X, 110.0f, 16.0f, 600);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_BACK);
}

static void elastic_drop(void *userdata)
{
    (void)userdata;
    AromaNode *box = IncenseFindWidget(g_registry, "box");
    if (!box)
        return;
    AromaAnimation *anim = aroma_animation_start(box, AROMA_ANIM_SLIDE_Y, 250.0f, 400.0f, 700);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
}

static void fade_toggle(void *userdata)
{
    (void)userdata;
    AromaNode *box = IncenseFindWidget(g_registry, "box");
    if (!box)
        return;
    g_faded = !g_faded;
    AromaAnimation *anim = aroma_animation_start(box, AROMA_ANIM_FADE, g_faded ? 1.0f : 0.0f, g_faded ? 0.0f : 1.0f, 500);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static void pulse(void *userdata)
{
    (void)userdata;
    AromaNode *box = IncenseFindWidget(g_registry, "box");
    if (!box)
        return;
    g_wide = !g_wide;
    AromaAnimation *anim = aroma_animation_start(box, AROMA_ANIM_SCALE_X, g_wide ? 220.0f : 280.0f, g_wide ? 280.0f : 220.0f, 500);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
}

int main(void)
{
    aroma_ui_init();
    aroma_animation_manager_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("slide_right", INCENSE_CALLBACK_VOID_PTR, slide_right, NULL);
    IncenseRegisterCallback("slide_back", INCENSE_CALLBACK_VOID_PTR, slide_back, NULL);
    IncenseRegisterCallback("elastic_drop", INCENSE_CALLBACK_VOID_PTR, elastic_drop, NULL);
    IncenseRegisterCallback("fade_toggle", INCENSE_CALLBACK_VOID_PTR, fade_toggle, NULL);
    IncenseRegisterCallback("pulse", INCENSE_CALLBACK_VOID_PTR, pulse, NULL);
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
