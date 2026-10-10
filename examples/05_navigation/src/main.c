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
static bool g_syncing;

static void note(const char *text)
{
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (status)
        aroma_label_set_text(status, text);
}

static void tabs_moved(AromaNode *node, int index, void *userdata)
{
    (void)node;
    (void)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "Tabs: page %d", index);
    note(buf);
    if (g_syncing)
        return;
    g_syncing = true;
    AromaNode *modes = IncenseFindWidget(g_registry, "modes");
    if (modes)
        aroma_segmented_set_selected(modes, index);
    g_syncing = false;
}

static void modes_moved(AromaNode *node, int index, void *userdata)
{
    (void)node;
    (void)userdata;
    if (g_syncing)
        return;
    g_syncing = true;
    AromaNode *pages = IncenseFindWidget(g_registry, "pages");
    if (pages)
        aroma_tabs_set_selected(pages, index);
    g_syncing = false;
}

static void played(void *userdata)
{
    note((const char *)userdata);
    AromaNode *track = IncenseFindWidget(g_registry, "track");
    if (track)
        aroma_label_set_text(track, (const char *)userdata);
}

static void toggle_drawer(void *userdata)
{
    (void)userdata;
    AromaNode *drawer = IncenseFindWidget(g_registry, "drawer");
    if (drawer)
        aroma_node_set_hidden(drawer, !drawer->is_hidden);
}

static void side_picked(AromaNode *node, int index, void *userdata)
{
    (void)node;
    (void)userdata;
    char buf[64];
    snprintf(buf, sizeof(buf), "Sidebar: item %d", index);
    note(buf);
    AromaNode *drawer = IncenseFindWidget(g_registry, "drawer");
    if (drawer)
        aroma_node_set_hidden(drawer, true);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("tabs_moved", INCENSE_CALLBACK_NODE_INT_PTR, tabs_moved, NULL);
    IncenseRegisterCallback("modes_moved", INCENSE_CALLBACK_NODE_INT_PTR, modes_moved, NULL);
    IncenseRegisterCallback("side_picked", INCENSE_CALLBACK_NODE_INT_PTR, side_picked, NULL);
    IncenseRegisterCallback("toggle_drawer", INCENSE_CALLBACK_VOID_PTR, toggle_drawer, NULL);
    IncenseRegisterCallback("play_lists", INCENSE_CALLBACK_VOID_PTR, played, "Playing all playlists");
    IncenseRegisterCallback("play_artists", INCENSE_CALLBACK_VOID_PTR, played, "Shuffling artists");
    IncenseRegisterCallback("play_albums", INCENSE_CALLBACK_VOID_PTR, played, "Playing latest albums");
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
