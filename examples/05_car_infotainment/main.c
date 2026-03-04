#include <aroma.h>

#include <unistd.h>
#include <stdio.h>

#define WIN_W 1280
#define WIN_H 800
AromaFont *icon_font = NULL;

static AromaFont *ui_font = NULL;
static AromaContainer *general_root = NULL;
static AromaNode *settings_root = NULL;
static AromaTheme theme;

int main(void)
{
    aroma_ui_init();
    aroma_splash(false);
    theme =
        aroma_theme_create_material_preset_dark(
            AROMA_THEME_MATERIAL_BLUE);
    aroma_ui_set_theme(&theme);

    ui_font =
        aroma_font_create_from_memory(
            aroma_ubuntu_ttf,
            aroma_ubuntu_ttf_len,
            16);
    icon_font =
        aroma_font_create_from_memory(
            icon_ttf,
            icon_ttf_len,
            24);
    AromaWindow *window =
        aroma_ui_create_window(
            "Automotive HMI",
            WIN_W, WIN_H);

    aroma_event_set_root((AromaNode *)window);
    aroma_ui_prepare_font_for_window(0, ui_font);

    aroma_ui_label(
        (AromaNode *)window,
        "12:45 PM",
        50, 30,
        LABEL_STYLE_LABEL_LARGE, ui_font);

    aroma_ui_label(
        (AromaNode *)window,
        "San Francisco, 68°F",
        150, 30,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_card((AromaNode *)window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, theme.colors.primary, icon_font);

    general_root = aroma_ui_container((AromaNode *)window, 125, 90, WIN_W - 250, WIN_H - 210, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)general_root, 20);

    build_general_ui(general_root);
    build_settings_ui((AromaNode *)window);
    AromaFont *tab_font = aroma_font_create_from_memory(
        icon_ttf, icon_ttf_len, 128);
    AromaNode *tabs = aroma_ui_tabs_with_icons((AromaNode *)window, 0, WIN_H - 80, WIN_W, 80,
                                               (const char *[]){"Main Screen", "Navigation", "Phone", "Settings"},
                                               (const char *[]){AROMA_ICON_DASHBOARD, AROMA_ICON_MAP, AROMA_ICON_PHONE, AROMA_ICON_SETTINGS},
                                               4, NULL, NULL, ui_font, tab_font);

    aroma_tabs_set_content(tabs, 0, (AromaNode **)&general_root, 1);
    aroma_tabs_set_content(tabs, 3, &settings_root, 1);

    while (aroma_ui_is_running())
    {

        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
    }

    aroma_ui_destroy_window(window);
    aroma_ui_unload_font(ui_font);
    aroma_ui_shutdown();
    return 0;
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h, NULL, NULL, ui_font);
    if (lv)
    {
        aroma_listview_set_icon_font(lv, icon_font);
    }
    return lv;
}

void build_settings_ui(AromaNode *window)
{
    int area_w = WIN_W - 250;
    int area_h = WIN_H - 210;
    int sidebar_w = 220;
    int panel_x = sidebar_w + 8;
    int panel_w = area_w - sidebar_w - 8;

    settings_root = aroma_container_create(window, 125, 90, area_w, area_h);

    const char *labels[] = {
        "Network",
        "Display",
        "Sound",
        "Navigation",
        "Security",
        "Apps",
        "Storage",
        "System",
        "About"};
    const char *icons[] = {
        AROMA_ICON_WIFI,
        AROMA_ICON_BRIGHTNESS_HIGH,
        AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP,
        AROMA_ICON_LOCK,
        AROMA_ICON_APPS,
        AROMA_ICON_STORAGE,
        AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO};
    int num_sections = 9;

    AromaNode *sidebar = aroma_ui_sidebar_with_icons(
        settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, ui_font, icon_font);
    AromaNode *lv_net = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_net, "Wi-Fi", "HomeNetwork_5G", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(lv_net, "Bluetooth", "3 devices paired", AROMA_ICON_BLUETOOTH, NULL);
    aroma_listview_add_item_with_icon(lv_net, "Hotspot & tethering", "Off", AROMA_ICON_WIFI, NULL);
    aroma_listview_add_item_with_icon(lv_net, "Mobile data", "1.2 GB used", AROMA_ICON_DATA_USAGE, NULL);
    aroma_listview_add_item_with_icon(lv_net, "Mobile network", "T-Mobile LTE", AROMA_ICON_NETWORK_CELL, NULL);
    AromaNode *p_net = aroma_listview_get_scroll_container(lv_net);

    AromaNode *lv_disp = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_disp, "Brightness level", "75%", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(lv_disp, "Adaptive brightness", "On", AROMA_ICON_BRIGHTNESS_AUTO, NULL);
    aroma_listview_add_item_with_icon(lv_disp, "Dark theme", "On", AROMA_ICON_INVERT_COLORS, NULL);
    aroma_listview_add_item_with_icon(lv_disp, "Auto-rotate screen", "On", AROMA_ICON_SCREEN_ROTATION, NULL);
    aroma_listview_add_item_with_icon(lv_disp, "Font size", "Medium", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(lv_disp, "Screen timeout", "5 minutes", AROMA_ICON_ACCESS_TIME, NULL);
    AromaNode *p_disp = aroma_listview_get_scroll_container(lv_disp);

    AromaNode *lv_snd = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_snd, "Media volume", "60%", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(lv_snd, "Navigation volume", "80%", AROMA_ICON_NAVIGATION, NULL);
    aroma_listview_add_item_with_icon(lv_snd, "Call volume", "90%", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(lv_snd, "Notification sound", "Pixie Dust", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(lv_snd, "Do Not Disturb", "Off", AROMA_ICON_DO_NOT_DISTURB, NULL);
    aroma_listview_add_item_with_icon(lv_snd, "Touch feedback", "On", AROMA_ICON_TUNE, NULL);
    AromaNode *p_snd = aroma_listview_get_scroll_container(lv_snd);

    AromaNode *lv_nav = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_nav, "Default navigation", "Built-in Maps", AROMA_ICON_MAP, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Location services", "On", AROMA_ICON_GPS_FIXED, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Live traffic", "On", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Satellite view", "Off", AROMA_ICON_LOCATION_ON, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Voice guidance", "On", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_separator(lv_nav);
    aroma_listview_add_item_with_icon(lv_nav, "Avoid toll roads", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Avoid highways", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(lv_nav, "Avoid ferries", NULL, AROMA_ICON_DIRECTIONS_CAR, NULL);
    AromaNode *p_nav = aroma_listview_get_scroll_container(lv_nav);

    AromaNode *lv_sec = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_sec, "Screen lock", "PIN", AROMA_ICON_LOCK, NULL);
    aroma_listview_add_item_with_icon(lv_sec, "Camera access", "Allowed", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(lv_sec, "Microphone access", "Allowed", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(lv_sec, "Location access", "Allowed", AROMA_ICON_LOCATION_ON, NULL);
    aroma_listview_add_item_with_icon(lv_sec, "Security scan", "Last scan: today", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(lv_sec, "Permission manager", NULL, AROMA_ICON_VERIFIED_USER, NULL);
    AromaNode *p_sec = aroma_listview_get_scroll_container(lv_sec);

    AromaNode *lv_app = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_app, "See all apps", "24 apps installed", AROMA_ICON_APPS, NULL);
    aroma_listview_add_item_with_icon(lv_app, "Notifications", "On", AROMA_ICON_NOTIFICATIONS, NULL);
    aroma_listview_add_item_with_icon(lv_app, "Default browser", "Chrome", AROMA_ICON_LINK, NULL);
    aroma_listview_add_item_with_icon(lv_app, "Special app access", NULL, AROMA_ICON_ACCESSIBILITY, NULL);
    AromaNode *p_app = aroma_listview_get_scroll_container(lv_app);

    AromaNode *lv_sto = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_sto, "Internal storage", "32 GB / 64 GB used", AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_separator(lv_sto);
    aroma_listview_add_item_with_icon(lv_sto, "Apps", "18.2 GB", AROMA_ICON_APPS, NULL);
    aroma_listview_add_item_with_icon(lv_sto, "Images & videos", "8.4 GB", AROMA_ICON_VISIBILITY, NULL);
    aroma_listview_add_item_with_icon(lv_sto, "Audio", "3.1 GB", AROMA_ICON_VOLUME_UP, NULL);
    aroma_listview_add_item_with_icon(lv_sto, "System", "2.3 GB", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(lv_sto, "SD card", "Not inserted", AROMA_ICON_SD_STORAGE, NULL);
    AromaNode *p_sto = aroma_listview_get_scroll_container(lv_sto);

    AromaNode *lv_sys = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(lv_sys, "Language", "English (US)", AROMA_ICON_LANGUAGE, NULL);
    aroma_listview_add_item_with_icon(lv_sys, "System update", "Up to date", AROMA_ICON_SYSTEM_UPDATE, NULL);
    aroma_listview_add_item_with_icon(lv_sys, "Backup", "Last: Mar 3, 2026", AROMA_ICON_BACKUP, NULL);
    aroma_listview_add_item_with_icon(lv_sys, "Reset options", NULL, AROMA_ICON_RESTORE, NULL);
    aroma_listview_add_item_with_icon(lv_sys, "Developer options", "Off", AROMA_ICON_DEVELOPER_MODE, NULL);
    AromaNode *p_sys = aroma_listview_get_scroll_container(lv_sys);

    AromaNode *lv_abt = settings_listview(settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_header(lv_abt, "Device Information");
    aroma_listview_add_item_with_icon(lv_abt, "Processor", "NaN", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "RAM", "NaN", AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_header(lv_abt, "Software Information");
    aroma_listview_add_item_with_icon(lv_abt, "Vehicle name", "Aroma Automotive", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Software", "AromaHMI v0.0.1 Built with AromaSDK", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Build number", "AA04032026", AROMA_ICON_BUILD, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Linux version", "5.10", AROMA_ICON_VERIFIED_USER, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Security patch", "March 1, 2026", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "UI framework", "AromaSDK", AROMA_ICON_FORMAT_PAINT, NULL);
    aroma_listview_add_header(lv_abt, "Legal");
    aroma_listview_add_item_with_icon(lv_abt, "License", "MIT License", AROMA_ICON_DESCRIPTION, NULL);
    aroma_listview_add_item_with_icon(lv_abt, "Source code", "github.com/BinaryInk/AromaUI", AROMA_ICON_LINK, NULL);

    AromaNode *p_abt = aroma_listview_get_scroll_container(lv_abt);

    aroma_sidebar_set_content(sidebar, 0, &p_net, 1);
    aroma_sidebar_set_content(sidebar, 1, &p_disp, 1);
    aroma_sidebar_set_content(sidebar, 2, &p_snd, 1);
    aroma_sidebar_set_content(sidebar, 3, &p_nav, 1);
    aroma_sidebar_set_content(sidebar, 4, &p_sec, 1);
    aroma_sidebar_set_content(sidebar, 5, &p_app, 1);
    aroma_sidebar_set_content(sidebar, 6, &p_sto, 1);
    aroma_sidebar_set_content(sidebar, 7, &p_sys, 1);
    aroma_sidebar_set_content(sidebar, 8, &p_abt, 1);

    aroma_sidebar_set_selected(sidebar, 8);

    aroma_node_set_hidden(settings_root, true);
}

void build_general_ui(AromaContainer *root)
{
    AromaNode *general_info = aroma_ui_card((AromaNode *)root, 0, 0, 400, 100, CARD_TYPE_FILLED);
    aroma_node_set_flex_grow(general_info, 1);

    AromaNode *applets_container = aroma_ui_container((AromaNode *)root, 0, 120, 610, 300, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    AromaNode *ac_applet = aroma_ui_card((AromaNode *)applets_container, 0, 0, 400, 300, CARD_TYPE_ELEVATED);
    aroma_node_set_gap((AromaNode *)applets_container, 20);
    AromaNode *applets_row = aroma_ui_container((AromaNode *)applets_container, 0, 130, 610, 270, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    AromaNode *applet1 = aroma_ui_card((AromaNode *)applets_row, 0, 0, 280, 70, CARD_TYPE_ELEVATED);
    AromaNode *applet2 = aroma_ui_image((AromaNode *)applets_row, "../test.png", 0, 0, 280, 70);
    aroma_node_set_gap((AromaNode *)applets_row, 20);
    aroma_node_set_flex_grow(applet1, 1);
    aroma_node_set_flex_grow(applet2, 1);

    aroma_ui_image(
        (AromaNode *)general_info,
        "../car.png",
        40, 20,
        264 * 1.2, 126 * 1.2);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Vehicle Status: All systems normal",
        90, 200,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    AromaNode *progress_bar = aroma_ui_progressbar(
        (AromaNode *)general_info,
        30, 260,
        360, 20,
        PROGRESS_TYPE_DETERMINATE, 0.75f);

    aroma_ui_icon((AromaNode *)general_info, AROMA_ICON_BATTERY_CHARGING_FULL, 40, 305, 48, theme.colors.primary, icon_font);

    AromaFont *font = aroma_font_create("../Ubuntu-Bold.ttf", 24);

    aroma_ui_label(
        (AromaNode *)general_info,
        "204",
        90, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "km",
        90 + aroma_font_get_line_width(font, "204"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Remaining",
        90, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);
    aroma_ui_divider(
        (AromaNode *)general_info,
        180, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)general_info,
        "128",
        200, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Wh",
        200 + aroma_font_get_line_width(font, "128"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Average",
        200, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_divider(
        (AromaNode *)general_info,
        290, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)general_info,
        "35.5",
        310, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "kWh",
        310 + aroma_font_get_line_width(font, "35.5"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Fuel Capacity",
        310, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    AromaNode *speed_card = aroma_ui_card(
        (AromaNode *)general_info,
        20, 380,
        200, 200, CARD_TYPE_FILLED);

    AromaFont *speed_font = aroma_font_create("../Ubuntu-Bold.ttf", 76);

    aroma_ui_label((AromaNode *)speed_card, "Speed", 20, 20, LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "88",
        20, 40,
        LABEL_STYLE_LABEL_LARGE, speed_font);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "km/h",
        20, 160,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_divider(
        (AromaNode *)speed_card,
        150, 20,
        170, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "Gear: Drive",
        170, 40,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    AromaNode *gear_card = aroma_ui_card(
        (AromaNode *)general_info,
        190, 450,
        200, 50, CARD_TYPE_ELEVATED);

    static const char *gears[4] = {"P", "R", "N", "D"};

    for (int i = 0; i < 4; ++i)
    {
        if (i == 3)
        {
            aroma_ui_card(
                (AromaNode *)gear_card,
                (i * 40) + 20, 10,
                40, 30, CARD_TYPE_FILLED);
        }
        AromaNode *label = aroma_ui_label(
            (AromaNode *)gear_card,
            gears[i],
            (i * 41) + 30, 8,
            LABEL_STYLE_LABEL_MEDIUM, font);
        aroma_node_set_z_index(label, 99999999);
    }

    AromaFont *ac_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 76);

    aroma_ui_label((AromaNode *)ac_applet, "Adjust AC to your comfort level", 20, 20, LABEL_STYLE_LABEL_LARGE, ui_font);
    aroma_ui_label((AromaNode *)ac_applet, "23°F", 225, 60, LABEL_STYLE_LABEL_LARGE, ac_font);

    aroma_ui_iconbutton((AromaNode *)ac_applet, AROMA_ICON_REMOVE, 145, 100, 48, ICON_BUTTON_FILLED, NULL, NULL, icon_font);
    aroma_ui_iconbutton((AromaNode *)ac_applet, AROMA_ICON_ADD, 400, 100, 48, ICON_BUTTON_FILLED, NULL, NULL, icon_font);

    aroma_ui_card((AromaNode *)ac_applet, 130, 200, 330, 80, CARD_TYPE_FILLED);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../air-conditioner.png",
        170, 215,
        48, 48);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../under.png",
        270, 215,
        48, 48);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../both.png",
        370, 215,
        48, 48);

    // System status

    aroma_ui_label((AromaNode *)applet1, "System Status", 20, 20, LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../brake_indicator.png",
        20, 60,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../abs_indicator.png",
        70, 60,
        32, 32);

    aroma_ui_card((AromaNode *)applet1, 115, 55, 42, 42, CARD_TYPE_FILLED);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../high_beams.png",
        120, 60,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../low_beams.png",
        170, 60,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../seatbelt.png",
        220, 60,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../battery_indicator.png",
        20, 120,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../temperature.png",
        70, 120,
        32, 32);
}
