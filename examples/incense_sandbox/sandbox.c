#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static void em_main_loop(void);
#endif

static AromaWindow *g_window = NULL;
static AromaFont *g_text_font = NULL;
static AromaFont *g_icon_font = NULL;
static IncenseRegistry *g_registry = NULL;
static int g_hot_reload_watcher = -1;

#ifdef __EMSCRIPTEN__
static void em_main_loop(void)
{
    if (!aroma_ui_is_running()) return;
    aroma_ui_process_events();
    if (g_window)
    {
        aroma_ui_render(g_window);
    }
}
#endif

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void aroma_sandbox_init(void)
{
    g_text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void aroma_sandbox_reload(const char *source)
{
    if (!source) return;

    AromaWindow *new_window = IncenseLoadStringEx(source, g_text_font, g_icon_font, &g_registry);
    if (!new_window)
    {
        return;
    }

    if (g_window)
    {
        aroma_ui_destroy_window(g_window);
        g_window = NULL;
    }

    g_window = new_window;
    AromaNode *root = (AromaNode *)g_window;
    aroma_event_set_root(root);
    aroma_node_invalidate_tree(root);
    aroma_ui_request_redraw(NULL);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char *aroma_sandbox_get_last_error(void)
{
    static char buf[512];
    int count = IncenseGetErrorCount();
    if (count <= 0) return "";
    const IncenseError *errs = IncenseGetErrors(&count);
    if (!errs || count <= 0) return "";
    snprintf(buf, sizeof(buf), "Line %d: %s", errs[0].line, errs[0].message);
    return buf;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_has_error(void)
{
    return IncenseHasFatalError() ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_get_width(void)
{
    if (!g_window) return 400;
    return g_window->rect.width;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int aroma_sandbox_get_height(void)
{
    if (!g_window) return 600;
    return g_window->rect.height;
}

static const char *default_source =
    "Window {\n"
    "    width: 320\n"
    "    height: 480\n"
    "    title: \"Settings\"\n"
    "\n"
    "    Container {\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "\n"
    "        Label {\n"
    "            text: \"Settings\"\n"
    "            style: large\n"
    "            color: #000000\n"
    "        }\n"
    "\n"
    "        Label {\n"
    "            text: \"Discover missing features in Incense\"\n"
    "            style: small\n"
    "            color: #666666\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 40\n"
    "            width: 320\n"
    "            height: 400\n"
    "\n"
    "            Header { text: \"General\" }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Airplane Mode\"\n"
    "                secondary: \"Off\"\n"
    "            }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Wi-Fi\"\n"
    "                secondary: \"Not Connected\"\n"
    "            }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Bluetooth\"\n"
    "                secondary: \"On\"\n"
    "            }\n"
    "\n"
    "            Separator { }\n"
    "\n"
    "            Header { text: \"Display & Brightness\" }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Brightness\"\n"
    "                secondary: \"50%\"\n"
    "            }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Auto-Lock\"\n"
    "                secondary: \"2 minutes\"\n"
    "            }\n"
    "\n"
    "            Separator { }\n"
    "\n"
    "            Header { text: \"Privacy\" }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Location Services\"\n"
    "                secondary: \"While Using the App\"\n"
    "            }\n"
    "\n"
    "            ListItem {\n"
    "                text: \"Photos\"\n"
    "                secondary: \"All Photos\"\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "}\n";

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    bool init_ok = aroma_ui_init();
    printf("aroma_ui_init: %s\n", init_ok ? "OK" : "FAILED");
    aroma_animation_manager_init();
    aroma_sandbox_init();

    printf("fonts: text=%p icon=%p\n", (void*)g_text_font, (void*)g_icon_font);

    aroma_sandbox_reload(default_source);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(g_window);
        #ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    if (g_window)
    {
        aroma_ui_destroy_window(g_window);
    }
    if (g_registry)
    {
        IncenseFreeRegistry(g_registry);
    }
    aroma_font_destroy(g_text_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_shutdown();

    return 0;
}
