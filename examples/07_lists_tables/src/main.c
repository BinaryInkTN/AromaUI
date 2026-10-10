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

static void listed(int index, void *userdata)
{
    (void)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "List: row %d picked", index);
    note(buf);
}

static void rowed(int row, void *userdata)
{
    (void)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "Table: row %d picked", row);
    note(buf);
}

static void invited(void *userdata)
{
    (void)userdata;
    note("Invites sent");
}

static void paged(int index, void *userdata)
{
    (void)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "Carousel: page %d", index);
    note(buf);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("listed", INCENSE_CALLBACK_INT_PTR, listed, NULL);
    IncenseRegisterCallback("paged", INCENSE_CALLBACK_INT_PTR, paged, NULL);
    IncenseRegisterCallback("invited", INCENSE_CALLBACK_VOID_PTR, invited, NULL);
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
    AromaNode *grid = IncenseFindWidget(g_registry, "grid");
    if (grid)
    {
        const char *cells[6][4] = {
            {"Ada", "Engineer", "L5", "London"},
            {"Grace", "Manager", "L6", "New York"},
            {"Linus", "Engineer", "L7", "Helsinki"},
            {"Ken", "Designer", "L4", "Kyoto"},
            {"Margaret", "QA", "L5", "Boston"},
            {"Rob", "Intern", "L3", "Tunis"}
        };
        aroma_table_set_callback(grid, rowed, NULL);
        for (int r = 0; r < 6; r++)
        {
            int row = aroma_table_add_row(grid);
            for (int c = 0; c < 4; c++)
                aroma_table_set_cell_text(grid, row, c, cells[r][c]);
        }
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
