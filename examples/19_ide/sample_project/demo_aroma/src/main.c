#include <aroma.h>
#include <aroma_incense_loader.h>
#include <unistd.h>
#include <stdio.h>

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;

static void new_note(void *user_data)
{
    (void)user_data;
    AromaNode *status = g_registry ? IncenseFindWidgetQuiet(g_registry, "status") : NULL;
    if (status)
        aroma_label_set_text(status, "New note added");
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_black();
    theme.colors.primary = 0xFFF97316u;
    theme.colors.background = 0xFF0F1115u;
    theme.colors.surface = 0xFF1A1D24u;
    theme.colors.text_primary = 0xFFE6E8EEu;
    theme.colors.text_secondary = 0xFF8B93A7u;
    theme.colors.border = 0xFF252B38u;
    aroma_ui_set_theme(&theme);

    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    if (!g_font || !g_icon_font)
        return 1;

    IncenseRegisterCallback("new_note", INCENSE_CALLBACK_VOID_PTR, new_note, NULL);
    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    if (!window)
    {
        int count = 0;
        const IncenseError *errors = IncenseGetErrors(&count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
        return 1;
    }

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
    }

    IncenseFreeRegistry(g_registry);
    aroma_font_destroy(g_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}
