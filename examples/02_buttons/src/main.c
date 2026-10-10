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

static void act(void *userdata)
{
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, (const char *)userdata);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("act_reply", INCENSE_CALLBACK_VOID_PTR, act, "Replying…");
    IncenseRegisterCallback("act_forward", INCENSE_CALLBACK_VOID_PTR, act, "Forwarding…");
    IncenseRegisterCallback("act_delete", INCENSE_CALLBACK_VOID_PTR, act, "Deleted");
    IncenseRegisterCallback("act_read", INCENSE_CALLBACK_VOID_PTR, act, "Marked read");
    IncenseRegisterCallback("act_snooze", INCENSE_CALLBACK_VOID_PTR, act, "Snoozed 1 hour");
    IncenseRegisterCallback("act_call", INCENSE_CALLBACK_VOID_PTR, act, "Calling sender…");
    IncenseRegisterCallback("act_video", INCENSE_CALLBACK_VOID_PTR, act, "Starting video…");
    IncenseRegisterCallback("act_pin", INCENSE_CALLBACK_VOID_PTR, act, "Pinned");
    IncenseRegisterCallback("act_more", INCENSE_CALLBACK_VOID_PTR, act, "More options…");
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
