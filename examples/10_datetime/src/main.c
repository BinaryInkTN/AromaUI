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
static int g_in_y = 2026;
static int g_in_m = 10;
static int g_in_d = 10;
static int g_out_y = 2026;
static int g_out_m = 10;
static int g_out_d = 17;
static int g_hour = 9;
static int g_minute = 30;

static void show(const char *text)
{
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, text);
}

static void summarize(void)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "Stay %04d-%02d-%02d to %04d-%02d-%02d, arrival %02d:%02d",
             g_in_y, g_in_m, g_in_d, g_out_y, g_out_m, g_out_d, g_hour, g_minute);
    show(buf);
}

static void picked_in(int year, int month, int day, void *userdata)
{
    (void)userdata;
    g_in_y = year;
    g_in_m = month;
    g_in_d = day;
    summarize();
}

static void picked_out(int year, int month, int day, void *userdata)
{
    (void)userdata;
    g_out_y = year;
    g_out_m = month;
    g_out_d = day;
    summarize();
}

static void picked_time(int hour, int minute, void *userdata)
{
    (void)userdata;
    g_hour = hour;
    g_minute = minute;
    summarize();
}

static void book(void *userdata)
{
    (void)userdata;
    AromaNode *dialog = IncenseFindWidget(g_registry, "booked");
    if (dialog)
        aroma_dialog_show(dialog);
}

static void close_dialog(void *userdata)
{
    (void)userdata;
    AromaNode *dialog = IncenseFindWidget(g_registry, "booked");
    if (dialog)
        aroma_dialog_hide(dialog);
    show("Booked, see you soon");
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("picked_in", INCENSE_CALLBACK_DATE_PTR, picked_in, NULL);
    IncenseRegisterCallback("picked_out", INCENSE_CALLBACK_DATE_PTR, picked_out, NULL);
    IncenseRegisterCallback("picked_time", INCENSE_CALLBACK_TIME_PTR, picked_time, NULL);
    IncenseRegisterCallback("book", INCENSE_CALLBACK_VOID_PTR, book, NULL);
    IncenseRegisterCallback("close_dialog", INCENSE_CALLBACK_VOID_PTR, close_dialog, NULL);
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
