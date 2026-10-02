#include "setup_wizard.h"
#include "setup_store.h"
#include "app_state.h"
#include "vehicle_view.h"
#include "theme_manager.h"
#include "aroma_animation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define SETUP_Z_ROOT 20000
#define SETUP_Z_CONTENT 20001
#define SETUP_PAGE_COUNT 6

#define SETUP_CONTENT_X 192
#define SETUP_CONTENT_W 640

static bool s_built = false;
static bool s_active = false;
static int s_page = 0;

static AromaNode *s_root = NULL;
static AromaNode *s_pages[SETUP_PAGE_COUNT] = {NULL};
static AromaNode *s_step_label = NULL;

static char s_device_name[128] = "";
static char s_wifi_ssid[128] = "";
static char s_wifi_pass[128] = "";
static AromaNode *s_wifi_status = NULL;
static AromaNode *s_summary_lines[5] = {NULL, NULL, NULL, NULL, NULL};

static const char *s_titles[SETUP_PAGE_COUNT] = {
    "Welcome",
    "Name your device",
    "Connect to Wi-Fi",
    "Bluetooth",
    "Display & voice",
    "You're all set",
};

static const char *s_subtitles[SETUP_PAGE_COUNT] = {
    "Let's set up your Aroma infotainment.",
    "This name identifies the car to phones and apps.",
    "Join a network for maps and updates.",
    "Let phones connect for calls and music.",
    "Make it yours. Changes apply instantly.",
    "Review your setup, then hit the road.",
};

static void setup_show_page(int page);

static void setup_refresh_chrome(void)
{
    if (!s_step_label)
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Step %d of %d", s_page + 1, SETUP_PAGE_COUNT);
    aroma_label_set_text(s_step_label, buf);
    for (int i = 0; i < SETUP_PAGE_COUNT; i++)
    {
        if (s_pages[i])
            aroma_node_set_hidden(s_pages[i], i != s_page);
    }
    if (s_root)
        aroma_node_invalidate(s_root);
    aroma_ui_request_redraw(NULL);
}

static bool on_setup_next(AromaNode *node, void *user_data);
static bool on_setup_back(AromaNode *node, void *user_data);
static bool on_setup_skip(AromaNode *node, void *user_data);

static void setup_add_footer(AromaNode *page, int index)
{
    int y = 500;
    if (index > 0)
    {
        aroma_ui_button(page, "Back", SETUP_CONTENT_X, y, 140, 48,
                        on_setup_back, NULL, state.ui_font);
    }
    aroma_ui_button(page, "Skip setup", SETUP_CONTENT_X + 320, y, 140, 48,
                    on_setup_skip, NULL, state.ui_font);
    aroma_ui_button(page, index == SETUP_PAGE_COUNT - 1 ? "Done" : "Next",
                    SETUP_CONTENT_X + 500, y, 140, 48,
                    on_setup_next, NULL, state.ui_font);
}

static bool on_setup_next(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!s_active)
        return true;
    if (s_page == 1)
    {
        setup_store_set("device_name", s_device_name[0] ? s_device_name : "Aroma Infotainment");
        vehicle_view_set_bt_device_name(setup_store_get("device_name", "Aroma Infotainment"));
    }
    else if (s_page == 2)
    {
        setup_store_set("wifi_ssid", s_wifi_ssid);
        setup_store_set("wifi_pass", s_wifi_pass);
    }
    else if (s_page == 3)
    {
        setup_store_set_int("bt_enabled", vehicle_view_is_bluetooth_enabled() ? 1 : 0);
    }
    else if (s_page == 4)
    {
        setup_store_set_int("dark_theme", state.dark_theme_enabled ? 1 : 0);
        setup_store_set_int("voice_enabled", state.g_voice_assistant_enabled ? 1 : 0);
    }
    setup_store_save();
    if (s_page < SETUP_PAGE_COUNT - 1)
    {
        if (s_page == 4)
        {
            char line[192];
            snprintf(line, sizeof(line), "Device: %s",
                     setup_store_get("device_name", "-"));
            if (s_summary_lines[0]) aroma_label_set_text(s_summary_lines[0], line);
            snprintf(line, sizeof(line), "Wi-Fi: %s",
                     setup_store_get("wifi_ssid", "(skipped)"));
            if (s_summary_lines[1]) aroma_label_set_text(s_summary_lines[1], line);
            snprintf(line, sizeof(line), "Bluetooth: %s",
                     setup_store_get_int("bt_enabled", 1) ? "on" : "off");
            if (s_summary_lines[2]) aroma_label_set_text(s_summary_lines[2], line);
            snprintf(line, sizeof(line), "Theme: %s",
                     setup_store_get_int("dark_theme", 0) ? "dark" : "light");
            if (s_summary_lines[3]) aroma_label_set_text(s_summary_lines[3], line);
            snprintf(line, sizeof(line), "Voice assistant: %s",
                     setup_store_get_int("voice_enabled", 1) ? "on" : "off");
            if (s_summary_lines[4]) aroma_label_set_text(s_summary_lines[4], line);
        }
        s_page++;
        setup_refresh_chrome();
    }
    else
    {
        setup_mark_complete();
        s_active = false;
        if (s_root)
            aroma_node_set_hidden(s_root, true);
        aroma_ui_request_redraw(NULL);
    }
    return true;
}

static bool on_setup_back(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (s_active && s_page > 0)
    {
        s_page--;
        setup_refresh_chrome();
    }
    return true;
}

static bool on_setup_skip(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!s_active)
        return true;
    setup_store_save();
    setup_mark_complete();
    s_active = false;
    if (s_root)
        aroma_node_set_hidden(s_root, true);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_device_name_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_device_name, sizeof(s_device_name), "%s", text ? text : "");
    return true;
}

static bool on_ssid_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", text ? text : "");
    return true;
}

static bool on_pass_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", text ? text : "");
    return true;
}

static void wifi_set_status(const char *text)
{
    if (s_wifi_status && text)
    {
        aroma_label_set_text(s_wifi_status, text);
        aroma_ui_request_redraw(NULL);
    }
}

static void shell_quote(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    if (o + 1 < out_len)
        out[o++] = '\'';
    for (const char *p = in; p && *p && o + 4 < out_len; p++)
    {
        if (*p == '\'')
        {
            memcpy(out + o, "'\\''", 4);
            o += 4;
        }
        else
        {
            out[o++] = *p;
        }
    }
    if (o + 1 < out_len)
        out[o++] = '\'';
    out[o < out_len ? o : out_len - 1] = '\0';
}

static void *wifi_connect_thread(void *arg)
{
    (void)arg;
    char ssid[128], pass[128];
    snprintf(ssid, sizeof(ssid), "%s", s_wifi_ssid);
    snprintf(pass, sizeof(pass), "%s", s_wifi_pass);
    if (!ssid[0])
    {
        wifi_set_status("Enter a network name first.");
        return NULL;
    }
    setup_store_set("wifi_ssid", ssid);
    setup_store_set("wifi_pass", pass);
    setup_store_save();
#ifdef __EMSCRIPTEN__
    wifi_set_status("Saved.");
    return NULL;
#else
    FILE *probe = popen("command -v nmcli 2>/dev/null", "r");
    bool have_nmcli = false;
    if (probe)
    {
        char buf[64] = "";
        if (fgets(buf, sizeof(buf), probe) && buf[0] != '\0')
            have_nmcli = true;
        pclose(probe);
    }
    if (!have_nmcli)
    {
        wifi_set_status("Saved. No network manager found.");
        return NULL;
    }
    wifi_set_status("Connecting...");
    char qssid[300], qpass[300], cmd[768];
    shell_quote(ssid, qssid, sizeof(qssid));
    shell_quote(pass, qpass, sizeof(qpass));
    snprintf(cmd, sizeof(cmd),
             "nmcli dev wifi connect %s password %s 2>&1 | tail -n 1",
             qssid, qpass);
    FILE *p = popen(cmd, "r");
    if (!p)
    {
        wifi_set_status("Saved. Connection failed to start.");
        return NULL;
    }
    char out[256] = "";
    if (fgets(out, sizeof(out), p))
    {
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
            out[--n] = '\0';
        char msg[320];
        snprintf(msg, sizeof(msg), "%s", out[0] ? out : "Done.");
        wifi_set_status(msg);
    }
    else
    {
        wifi_set_status("Saved.");
    }
    pclose(p);
    return NULL;
#endif
}

static bool on_wifi_connect(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, wifi_connect_thread, NULL) != 0)
        wifi_set_status("Saved. Could not start connector.");
    else
        wifi_set_status("Connecting...");
    pthread_attr_destroy(&attr);
    return true;
}

static bool on_setup_bt_changed(AromaNode *node, void *user_data)
{
    (void)user_data;
    vehicle_view_set_bluetooth_enabled(aroma_switch_get_state(node));
    return true;
}

static bool on_setup_theme_dark(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    apply_theme(true);
    return true;
}

static bool on_setup_theme_light(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    apply_theme(false);
    return true;
}

static bool on_setup_voice_changed(AromaNode *node, void *user_data)
{
    (void)user_data;
    state.g_voice_assistant_enabled = aroma_switch_get_state(node);
    return true;
}

static AromaNode *setup_new_page(void)
{
    AromaNode *page = aroma_ui_container(
        s_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_START);
    aroma_node_set_z_index(page, SETUP_Z_CONTENT);
    aroma_node_set_hidden(page, true);
    return page;
}

static void setup_add_header(AromaNode *page, int index)
{
    aroma_ui_label(page, s_titles[index], SETUP_CONTENT_X, 120,
                   LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_ui_label(page, s_subtitles[index], SETUP_CONTENT_X, 170,
                   LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
}

static void setup_build(void)
{
    if (s_built)
        return;
    s_built = true;

    s_root = aroma_ui_container(
        (AromaNode *)state.window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_START);
    aroma_node_set_z_index(s_root, SETUP_Z_ROOT);

    AromaNode *bg = aroma_ui_card(s_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_FILLED);
    aroma_card_set_colors(bg, 0xFF101418, 0xFF101418);
    aroma_node_set_z_index(bg, SETUP_Z_CONTENT);

    s_step_label = aroma_ui_label(s_root, "", SETUP_CONTENT_X, 60,
                                  LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_label_set_color(s_step_label, 0xFF8E8E93);
    aroma_node_set_z_index(s_step_label, SETUP_Z_CONTENT);

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 0);
        aroma_ui_icon(p, AROMA_ICON_DIRECTIONS_CAR, SETUP_CONTENT_X, 240, 96,
                      0xFF007AFF, state.huge_icon_font);
        aroma_ui_label(p, "Maps, music, calls and apps -",
                       SETUP_CONTENT_X, 360, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        aroma_ui_label(p, "ready in under a minute.",
                       SETUP_CONTENT_X, 390, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        setup_add_footer(p, 0);
        s_pages[0] = p;
    }

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 1);
        snprintf(s_device_name, sizeof(s_device_name), "%s",
                 setup_store_get("device_name", "Aroma Infotainment"));
        AromaNode *name_box = aroma_ui_textbox(p, SETUP_CONTENT_X, 250, 420, 48, "Device name",
                         on_device_name_changed, NULL, state.ui_font);
        aroma_textbox_set_text(name_box, s_device_name);
        aroma_ui_label(p, "Used for Bluetooth pairing.",
                       SETUP_CONTENT_X, 310, LABEL_STYLE_LABEL_SMALL, state.ui_font);
        setup_add_footer(p, 1);
        s_pages[1] = p;
    }

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 2);
        snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s",
                 setup_store_get("wifi_ssid", ""));
        AromaNode *ssid_box = aroma_ui_textbox(p, SETUP_CONTENT_X, 240, 420, 48, "Network name (SSID)",
                         on_ssid_changed, NULL, state.ui_font);
        if (s_wifi_ssid[0])
            aroma_textbox_set_text(ssid_box, s_wifi_ssid);
        aroma_ui_textbox(p, SETUP_CONTENT_X, 300, 420, 48, "Password",
                         on_pass_changed, NULL, state.ui_font);
        aroma_ui_button(p, "Connect", SETUP_CONTENT_X, 364, 160, 48,
                        on_wifi_connect, NULL, state.ui_font);
        s_wifi_status = aroma_ui_label(p, "", SETUP_CONTENT_X, 424,
                                       LABEL_STYLE_LABEL_SMALL, state.ui_font);
        setup_add_footer(p, 2);
        s_pages[2] = p;
    }

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 3);
        bool bt_on = vehicle_view_is_bluetooth_enabled() ||
                     setup_store_get_int("bt_enabled", 1);
        aroma_ui_switch(p, SETUP_CONTENT_X, 250, 60, 30, bt_on,
                        on_setup_bt_changed, NULL);
        aroma_ui_label(p, "Allow phones to connect",
                       SETUP_CONTENT_X + 80, 252,
                       LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        setup_add_footer(p, 3);
        s_pages[3] = p;
    }

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 4);
        aroma_ui_button(p, "Dark", SETUP_CONTENT_X, 250, 160, 48,
                        on_setup_theme_dark, NULL, state.ui_font);
        aroma_ui_button(p, "Light", SETUP_CONTENT_X + 180, 250, 160, 48,
                        on_setup_theme_light, NULL, state.ui_font);
        bool voice_on = state.g_voice_assistant_enabled;
        aroma_ui_switch(p, SETUP_CONTENT_X, 330, 60, 30, voice_on,
                        on_setup_voice_changed, NULL);
        aroma_ui_label(p, "Voice assistant",
                       SETUP_CONTENT_X + 80, 332,
                       LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        setup_add_footer(p, 4);
        s_pages[4] = p;
    }

    {
        AromaNode *p = setup_new_page();
        setup_add_header(p, 5);
        for (int i = 0; i < 5; i++)
        {
            s_summary_lines[i] = aroma_ui_label(p, "", SETUP_CONTENT_X, 250 + i * 32,
                                                LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        }
        setup_add_footer(p, 5);
        s_pages[5] = p;
    }

    vehicle_view_set_bluetooth_enabled(setup_store_get_int("bt_enabled", 1) != 0);
    aroma_node_set_hidden(s_root, true);
    vehicle_view_raise_subtree(s_root);
}

static void setup_show_page(int page)
{
    s_page = page;
    setup_refresh_chrome();
}

void setup_wizard_maybe_show(void)
{
    const char *fresh = getenv("AROMA_FIRST_RUN");
    if (fresh && fresh[0] && strcmp(fresh, "0") != 0)
    {
        setup_wizard_show();
        return;
    }
    if (setup_is_complete())
        return;
    setup_wizard_show();
}

void setup_wizard_show(void)
{
    setup_build();
    s_page = 0;
    s_active = true;
    aroma_node_set_hidden(s_root, false);
    setup_show_page(0);
    aroma_ui_request_redraw(NULL);
}

bool setup_wizard_is_active(void)
{
    return s_active;
}
