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

static void note(const char *text)
{
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, text);
}

static void show_dialog(void *userdata)
{
    (void)userdata;
    AromaNode *dialog = IncenseFindWidget(g_registry, "dlg");
    if (dialog)
        aroma_dialog_show(dialog);
}

static void dialog_done(void *userdata)
{
    note((const char *)userdata);
    AromaNode *dialog = IncenseFindWidget(g_registry, "dlg");
    if (dialog)
        aroma_dialog_hide(dialog);
}

static void show_snackbar(void *userdata)
{
    (void)userdata;
    note("Snackbar: shown");
    AromaNode *snack = IncenseFindWidget(g_registry, "snack");
    if (snack)
        aroma_snackbar_show(snack);
}

static void snack_undo(void *userdata)
{
    (void)userdata;
    note("Snackbar: undone");
}

static void snack_open(void *userdata)
{
    (void)userdata;
    note("Snackbar: opening file");
}

static void sort_by(void *userdata)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "Menu: %s", (const char *)userdata);
    note(buf);
    AromaNode *menu = IncenseFindWidget(g_registry, "menu");
    if (menu)
        aroma_menu_hide(menu);
}

static void show_menu(void *userdata)
{
    (void)userdata;
    AromaNode *menu = IncenseFindWidget(g_registry, "menu");
    if (menu)
        aroma_menu_show(menu);
}

static bool fill_bar(AromaNode *node, void *userdata)
{
    (void)userdata;
    AromaNode *bar = IncenseFindWidget(g_registry, "bar");
    if (bar)
        aroma_progressbar_set_progress(bar, aroma_slider_get_value(node) / 100.0f);
    return true;
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("show_dialog", INCENSE_CALLBACK_VOID_PTR, show_dialog, NULL);
    IncenseRegisterCallback("dlg_ok", INCENSE_CALLBACK_VOID_PTR, dialog_done, "Dialog: confirmed");
    IncenseRegisterCallback("dlg_cancel", INCENSE_CALLBACK_VOID_PTR, dialog_done, "Dialog: dismissed");
    IncenseRegisterCallback("show_snackbar", INCENSE_CALLBACK_VOID_PTR, show_snackbar, NULL);
    IncenseRegisterCallback("snack_undo", INCENSE_CALLBACK_VOID_PTR, snack_undo, NULL);
    IncenseRegisterCallback("show_menu", INCENSE_CALLBACK_VOID_PTR, show_menu, NULL);
    IncenseRegisterCallback("dlg_archive", INCENSE_CALLBACK_VOID_PTR, dialog_done, "Dialog: archived");
    IncenseRegisterCallback("snack_open", INCENSE_CALLBACK_VOID_PTR, snack_open, NULL);
    IncenseRegisterCallback("menu_name", INCENSE_CALLBACK_VOID_PTR, sort_by, "sort by name");
    IncenseRegisterCallback("menu_date", INCENSE_CALLBACK_VOID_PTR, sort_by, "sort by date");
    IncenseRegisterCallback("menu_size", INCENSE_CALLBACK_VOID_PTR, sort_by, "sort by size");
    IncenseRegisterCallback("menu_refresh", INCENSE_CALLBACK_VOID_PTR, sort_by, "list refreshed");
    IncenseRegisterCallback("fill_bar", INCENSE_CALLBACK_BOOL_PTR, fill_bar, NULL);
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
