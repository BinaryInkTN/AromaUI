#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define WIN_W 320
#define WIN_H 480

extern void aroma_sandbox_init(void);
extern void aroma_sandbox_reload(const char *source);
extern const char *aroma_sandbox_get_last_error(void);
extern int aroma_sandbox_has_error(void);
extern int aroma_sandbox_get_width(void);
extern int aroma_sandbox_get_height(void);

static AromaWindow *g_window = NULL;
static AromaFont *g_text_font = NULL;
static AromaFont *g_icon_font = NULL;
static IncenseRegistry *g_registry = NULL;

#define MAX_PAGES 16
static AromaNode *g_pages[MAX_PAGES];
static const char *g_page_ids[MAX_PAGES];
static int g_page_count = 0;
static AromaNode *g_current_page = NULL;
static AromaNode *g_from_page = NULL;
static bool g_is_animating = false;

static bool g_search_active = false;
static char g_search_text[128] = {0};

static int strcase_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return 0;
    while (*haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n))
        {
            h++;
            n++;
        }
        if (!*n) return 1;
        haystack++;
    }
    return 0;
}

static bool on_search_change(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!g_registry) return false;

    if (text && strlen(text) > 0) {
        strncpy(g_search_text, text, sizeof(g_search_text) - 1);
        g_search_text[sizeof(g_search_text) - 1] = '\0';
        g_search_active = true;
    } else {
        g_search_text[0] = '\0';
        g_search_active = false;
    }

    AromaNode *list = IncenseFindWidget(g_registry, "settings_list");
    if (!list) return false;

    size_t count = aroma_listview_get_count(list);
    for (size_t i = 0; i < count; i++) {
        bool should_show = !g_search_active;
        if (g_search_active) {
            const char *item_text = aroma_listview_get_item_text(list, (int)i);
            if (item_text && strcase_contains(item_text, g_search_text)) {
                should_show = true;
            } else {
                should_show = false;
            }
        }
        aroma_listview_set_item_hidden(list, (int)i, !should_show);
    }
    aroma_node_update_layout(list, 0, 120, WIN_W, 360);
    aroma_node_invalidate(list);
    aroma_ui_request_redraw(NULL);
    return true;
}

static void on_wifi_connect(void *user_data)
{
    (void)user_data;
}

static void navigate_to_detail(int index, void *user_data)
{
    (void)index;
    const char *target_id = (const char *)user_data;
    AromaNode *target = target_id ? IncenseFindWidget(g_registry, target_id) : NULL;
    if (!target) return;

    if (g_current_page) {
        g_from_page = g_current_page;
        aroma_node_set_hidden(g_current_page, true);
    }

    aroma_node_set_hidden(target, false);
    aroma_node_update_layout(target, 0, 0, WIN_W, WIN_H);
    g_current_page = target;
    g_is_animating = false;
    aroma_node_invalidate(target);
    aroma_ui_request_redraw(NULL);
}

static void navigate_to_main(void *user_data)
{
    (void)user_data;
    if (!g_registry) return;

    AromaNode *main = IncenseFindWidget(g_registry, "page_main");
    if (!main) return;

    if (g_current_page && g_current_page != main) {
        g_from_page = g_current_page;
    }
    aroma_node_set_hidden(main, false);
    aroma_node_update_layout(main, 0, 0, WIN_W, WIN_H);
    g_current_page = main;
    g_is_animating = false;
    aroma_node_invalidate(main);
    aroma_ui_request_redraw(NULL);
}

static void navigate_to_wifi_connect(void *user_data)
{
    (void)user_data;
    navigate_to_detail(0, (void *)"page_wifi_connect");
}

static void register_callbacks(void)
{
    IncenseRegisterCallback("navigate", INCENSE_CALLBACK_INT_PTR, (void *)navigate_to_detail, NULL);
    IncenseRegisterCallback("back", INCENSE_CALLBACK_VOID_PTR, (void *)navigate_to_main, NULL);
    IncenseRegisterCallback("navigate_wifi_connect", INCENSE_CALLBACK_VOID_PTR, (void *)navigate_to_wifi_connect, NULL);
    IncenseRegisterCallback("wifi_connect", INCENSE_CALLBACK_VOID_PTR, (void *)on_wifi_connect, NULL);
    IncenseRegisterCallback("on_search_change", INCENSE_CALLBACK_NODE_STRING_PTR, (void *)on_search_change, NULL);
}

static const char *default_source =
    "Window {\n"
    "    width: 320\n"
    "    height: 480\n"
    "    title: \"Settings\"\n"
    "\n"
    "    Container {\n"
    "        id: \"page_main\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 1\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 120\n"
    "        \n"
    "            color: #121212\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "                visible: 0\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Settings\"\n"
    "                style: large\n"
    "                color: #FFFFFF\n"
    "                y:20\n"
    "                x: 10\n"
    "            }\n"
    "             Textbox {\n"
    "            text: \"\"\n"
    "            placeholder: \"Search settings\"\n"
    "            x: 10\n"
    "            y: 56\n"
    "            width: 300\n"
    "            height: 44\n"
    "            on_change: \"on_search_change\"\n"
    "        }\n"
    "        }\n"
    "\n"
    "       \n"
    "        ListView {\n"
    "            id: \"settings_list\"\n"
    "            x: 0\n"
    "            y: 120\n"
    "            width: 320\n"
    "            height: 360\n"
    "            on_select: \"navigate\"\n"
    "\n"
    "            Header { text: \"General\" }\n"
    "            ListItem { text: \"Airplane Mode\" secondary: \"Off\" icon: \"AROMA_ICON_AIRPLANEMODE_INACTIVE\" }\n"
    "            ListItem { text: \"Wi-Fi\" secondary: \"Not Connected\" icon: \"AROMA_ICON_NETWORK_WIFI\" }\n"
    "            ListItem { text: \"Bluetooth\" secondary: \"On\" icon: \"AROMA_ICON_BLUETOOTH\" }\n"
    "            ListItem { text: \"Cellular\" secondary: \"5G On\" icon: \"AROMA_ICON_SIGNAL_CELLULAR_4_BAR\" }\n"
    "            ListItem { text: \"VPN\" secondary: \"Disconnected\" icon: \"AROMA_ICON_LOCK\" }\n"
    "\n"
    "            Header { text: \"Display & Brightness\" }\n"
    "            ListItem { text: \"Brightness\" secondary: \"50%\" icon: \"AROMA_ICON_BRIGHTNESS_MEDIUM\" }\n"
    "            ListItem { text: \"Auto-Lock\" secondary: \"2 minutes\" icon: \"AROMA_ICON_LOCK\" }\n"
    "            ListItem { text: \"Night Shift\" secondary: \"On until 7 AM\" icon: \"AROMA_ICON_WB_SUNNY\" }\n"
    "            ListItem { text: \"True Tone\" secondary: \"On\" icon: \"AROMA_ICON_BRIGHTNESS_AUTO\" }\n"
    "\n"
    "            Header { text: \"Privacy\" }\n"
    "            ListItem { text: \"Location Services\" secondary: \"While Using the App\" icon: \"AROMA_ICON_LOCATION_CITY\" }\n"
    "            ListItem { text: \"Photos\" secondary: \"All Photos\" icon: \"AROMA_ICON_PHOTO\" }\n"
    "            ListItem { text: \"Camera\" secondary: \"On\" icon: \"AROMA_ICON_PHOTO_CAMERA\" }\n"
    "            ListItem { text: \"Microphone\" secondary: \"On\" icon: \"AROMA_ICON_MIC\" }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_airplane\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "    \n"
    "            color: #121212\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Airplane Mode\"\n"
    "                style: large\n"
    "                color: #FFFFFF\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 424\n"
    "\n"
    "            ListItem { text: \"Airplane Mode\" secondary: \"Off\" }\n"
    "            ListItem { text: \"My Number\" secondary: \"\" }\n"
    "            ListItem { text: \"Cellular Data Options\" secondary: \"\" }\n"
    "            ListItem { text: \"Cellular Data\" secondary: \"On\" }\n"
    "            ListItem { text: \"Data Roaming\" secondary: \"Off\" }\n"
    "            ListItem { text: \"Voice & Data\" secondary: \"5G Auto\" }\n"
    "            ListItem { text: \"Data Mode\" secondary: \"Allow More Data on 5G\" }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_wifi\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "  \n"
    "            color: #121212\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Wi-Fi\"\n"
    "                style: large\n"
    "                color: #FFFFFF\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 370\n"
    "\n"
    "            ListItem { text: \"Wi-Fi\" secondary: \"On\" }\n"
    "            ListItem { text: \"Network Name\" secondary: \"Not Connected\" }\n"
    "            ListItem { text: \"Auto-Join\" secondary: \"On\" }\n"
    "            ListItem { text: \"Auto-Lock\" secondary: \"Never\" }\n"
    "            ListItem { text: \"Ask to Join\" secondary: \"On\" }\n"
    "        }\n"
    "\n"
    "        IconButton {\n"
    "            x: 246\n"
    "            y: 394\n"
    "            width: 60\n"
    "            height: 60\n"
    "            icon: \"AROMA_ICON_ADD\"\n"
    "            variant: standard\n"
    "            on_click: \"navigate_wifi_connect\"\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_wifi_connect\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "            color: #121212\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Wi-Fi\"\n"
    "                style: large\n"
    "                color: #FFFFFF\n"
    "                y: 20\n"
    "                x: 60\n"
    "            }\n"
    "        }\n"
    "\n"
    "        Container {\n"
    "            x: 20\n"
    "            y: 70\n"
    "            width: 280\n"
    "            height: 340\n"
    "\n"
    "            Textbox {\n"
    "                text: \"\"\n"
    "                placeholder: \"Network Name (SSID)\"\n"
    "                x: 20\n"
    "                y: 30\n"
    "                width: 240\n"
    "                height: 44\n"
    "            }\n"
    "\n"
    "            Textbox {\n"
    "                text: \"\"\n"
    "                placeholder: \"Password\"\n"
    "                x: 20\n"
    "                y: 90\n"
    "                width: 240\n"
    "                height: 44\n"
    "            }\n"
    "\n"
    "            Button {\n"
    "                text: \"Connect to Network\"\n"
    "                x: 40\n"
    "                y: 170\n"
    "                width: 200\n"
    "                height: 44\n"
    "                on_click: \"wifi_connect\"\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    Container {\n"
    "        id: \"page_bluetooth\"\n"
    "        x: 0\n"
    "        y: 0\n"
    "        width: 320\n"
    "        height: 480\n"
    "        layout: flex\n"
    "        direction: column\n"
    "        visible: 0\n"
    "\n"
    "        Container {\n"
    "            x: 0\n"
    "            y: 0\n"
    "            width: 320\n"
    "            height: 56\n"
    "            color: #121212\n"
    "\n"
    "            IconButton {\n"
    "                x: 8\n"
    "                y: 8\n"
    "                width: 40\n"
    "                height: 40\n"
    "                icon: \"AROMA_ICON_ARROW_BACK\"\n"
    "                variant: standard\n"
    "                on_click: \"back\"\n"
    "            }\n"
    "\n"
    "            Label {\n"
    "                text: \"Bluetooth\"\n"
    "                style: large\n"
    "                color: #FFFFFF\n"
    "                y: 20\n"
    "                x: 60 \n"
    "            }\n"
    "        }\n"
    "\n"
    "        ListView {\n"
    "            x: 0\n"
    "            y: 56\n"
    "            width: 320\n"
    "            height: 424\n"
    "\n"
    "            ListItem { text: \"Bluetooth\" secondary: \"On\" }\n"
    "            ListItem { text: \"My Devices\" secondary: \"\" }\n"
    "            ListItem { text: \"Other Devices\" secondary: \"\" }\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    ;

int aroma_sandbox_get_width(void)
{
    if (!g_window) return WIN_W;
    return g_window->rect.width;
}

int aroma_sandbox_get_height(void)
{
    if (!g_window) return WIN_H;
    return g_window->rect.height;
}

void aroma_sandbox_init(void)
{
    g_text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);
    register_callbacks();
}

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
    }
    g_window = new_window;

    g_current_page = IncenseFindWidget(g_registry, "page_main");
    if (g_current_page) {
        aroma_node_update_layout(g_current_page, 0, 0, WIN_W, WIN_H);
    }

    g_page_count = 0;
}

const char *aroma_sandbox_get_last_error(void)
{
    static char errbuf[512];
    int count = IncenseGetErrorCount();
    if (count <= 0) return "";
    const IncenseError *errs = IncenseGetErrors(&count);
    if (!errs || count <= 0) return "";
    snprintf(errbuf, sizeof(errbuf), "Line %d: %s", errs[0].line, errs[0].message);
    return errbuf;
}

int aroma_sandbox_has_error(void)
{
    return IncenseHasFatalError() ? 1 : 0;
}

static void em_main_loop(void)
{
    aroma_ui_process_events();
    aroma_ui_render(g_window);
}

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

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(em_main_loop, 60, 1);
#else
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(g_window);
        usleep(16000);
    }
#endif

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
