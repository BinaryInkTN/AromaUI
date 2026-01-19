#include "aroma.h"
#include <unistd.h>
#include <stdio.h>

static AromaFont* font = NULL;
static AromaNode* sidebar = NULL;
static AromaNode* tabs = NULL;
static AromaNode* content_root = NULL;
static AromaNode* section_network = NULL;
static AromaNode* section_display = NULL;
static AromaNode* section_sound = NULL;
static AromaNode* section_power = NULL;
static AromaNode* section_about = NULL;
static AromaNode* tab_general_container = NULL;
static AromaNode* tab_advanced_container = NULL;
static AromaNode* tab_security_container = NULL;
static AromaNode* header_label = NULL;
static AromaNode* wifi_label = NULL;
static AromaNode* wifi_switch = NULL;
static AromaNode* bluetooth_label = NULL;
static AromaNode* bluetooth_switch = NULL;
static AromaNode* brightness_label = NULL;
static AromaNode* brightness_slider = NULL;
static AromaNode* debug_overlay = NULL;
static AromaNode* section_display_label = NULL;
static AromaNode* section_sound_label = NULL;
static AromaNode* section_power_label = NULL;
static AromaNode* section_about_label = NULL;
static AromaNode* tab_general_label = NULL;
static AromaNode* tab_advanced_label = NULL;
static AromaNode* tab_security_label = NULL;
static AromaNode* network_dropdown = NULL;
static AromaNode* device_name_textbox = NULL;
static AromaNode* auto_connect_checkbox = NULL;
static AromaNode* connect_button = NULL;
static AromaNode* signal_progress = NULL;
static AromaNode* band_radio_24 = NULL;
static AromaNode* band_radio_5 = NULL;
static AromaNode* devices_list = NULL;
static AromaNode* security_chip = NULL;
static AromaNode* security_card = NULL;
static AromaNode* security_fab = NULL;
static AromaNode* security_snackbar = NULL;
static AromaNode* power_dialog = NULL;
static AromaNode* about_menu = NULL;
static AromaNode* about_tooltip = NULL;
static AromaNode* about_icon_button = NULL;
static AromaNode* about_divider = NULL;
static AromaNode* display_progress = NULL;
static AromaNode* sound_slider = NULL;
static AromaNode* power_button = NULL;

static AromaNode* create_container(AromaNode* parent, int x, int y, int width, int height)
{
    if (!parent) return NULL;
    AromaRect* rect = (AromaRect*)aroma_widget_alloc(sizeof(AromaRect));
    if (!rect) return NULL;
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    AromaNode* node = __add_child_node(NODE_TYPE_CONTAINER, parent, rect);
    if (!node) {
        aroma_widget_free(rect);
        return NULL;
    }
    return node;
}

static const char* sidebar_items[] = {
    "Network",
    "Display",
    "Sound",
    "Power",
    "About"
};

static const char* tab_items[] = {
    "General",
    "Advanced",
    "Security"
};

static void on_sidebar_select(AromaNode* node, int index, void* user_data)
{
    (void)node;
    (void)user_data;
    if (!header_label) return;
    if (index < 0 || index >= (int)(sizeof(sidebar_items) / sizeof(sidebar_items[0]))) return;
    aroma_label_set_text(header_label, sidebar_items[index]);
    aroma_ui_request_redraw(NULL);
}

static void on_tabs_change(AromaNode* node, int index, void* user_data)
{
    (void)node;
    (void)user_data;
    if (!header_label) return;

    int sidebar_index = sidebar ? aroma_sidebar_get_selected(sidebar) : -1;
    if (sidebar_index < 0 || sidebar_index >= (int)(sizeof(sidebar_items) / sizeof(sidebar_items[0]))) {
        sidebar_index = 0;
    }

    char title[64];
    snprintf(title, sizeof(title), "%s - %s", sidebar_items[sidebar_index], tab_items[index]);
    aroma_label_set_text(header_label, title);
    aroma_ui_request_redraw(NULL);
}

static void window_update_callback(size_t window_id, void* data)
{
    (void)data;
    if (!aroma_ui_consume_redraw()) return;

    AromaTheme theme = aroma_theme_get_global();
    aroma_graphics_clear(window_id, theme.colors.background);

    size_t dirty_count = 0;
    aroma_dirty_list_get(&dirty_count);
    if (dirty_count > 0 || aroma_ui_is_immediate_mode()) {
        if (sidebar && !aroma_node_is_hidden(sidebar)) aroma_sidebar_draw(sidebar, window_id);
        if (tabs && !aroma_node_is_hidden(tabs)) aroma_tabs_draw(tabs, window_id);
        if (header_label && !aroma_node_is_hidden(header_label)) aroma_label_draw(header_label, window_id);
        if (wifi_label && !aroma_node_is_hidden(wifi_label)) aroma_label_draw(wifi_label, window_id);
        if (wifi_switch && !aroma_node_is_hidden(wifi_switch)) aroma_switch_draw(wifi_switch, window_id);
        if (bluetooth_label && !aroma_node_is_hidden(bluetooth_label)) aroma_label_draw(bluetooth_label, window_id);
        if (bluetooth_switch && !aroma_node_is_hidden(bluetooth_switch)) aroma_switch_draw(bluetooth_switch, window_id);
        if (brightness_label && !aroma_node_is_hidden(brightness_label)) aroma_label_draw(brightness_label, window_id);
        if (brightness_slider && !aroma_node_is_hidden(brightness_slider)) aroma_slider_draw(brightness_slider, window_id);
        if (section_display_label && !aroma_node_is_hidden(section_display_label)) aroma_label_draw(section_display_label, window_id);
        if (section_sound_label && !aroma_node_is_hidden(section_sound_label)) aroma_label_draw(section_sound_label, window_id);
        if (section_power_label && !aroma_node_is_hidden(section_power_label)) aroma_label_draw(section_power_label, window_id);
        if (section_about_label && !aroma_node_is_hidden(section_about_label)) aroma_label_draw(section_about_label, window_id);
        if (tab_general_label && !aroma_node_is_hidden(tab_general_label)) aroma_label_draw(tab_general_label, window_id);
        if (tab_advanced_label && !aroma_node_is_hidden(tab_advanced_label)) aroma_label_draw(tab_advanced_label, window_id);
        if (tab_security_label && !aroma_node_is_hidden(tab_security_label)) aroma_label_draw(tab_security_label, window_id);
        if (network_dropdown && !aroma_node_is_hidden(network_dropdown)) aroma_dropdown_draw(network_dropdown, window_id);
        if (device_name_textbox && !aroma_node_is_hidden(device_name_textbox)) aroma_textbox_draw(device_name_textbox, window_id);
        if (auto_connect_checkbox && !aroma_node_is_hidden(auto_connect_checkbox)) aroma_checkbox_draw(auto_connect_checkbox, window_id);
        if (connect_button && !aroma_node_is_hidden(connect_button)) aroma_button_draw(connect_button, window_id);
        if (signal_progress && !aroma_node_is_hidden(signal_progress)) aroma_progressbar_draw(signal_progress, window_id);
        if (band_radio_24 && !aroma_node_is_hidden(band_radio_24)) aroma_radiobutton_draw(band_radio_24, window_id);
        if (band_radio_5 && !aroma_node_is_hidden(band_radio_5)) aroma_radiobutton_draw(band_radio_5, window_id);
        if (devices_list && !aroma_node_is_hidden(devices_list)) aroma_listview_draw(devices_list, window_id);
        if (security_chip && !aroma_node_is_hidden(security_chip)) aroma_chip_draw(security_chip, window_id);
        if (security_card && !aroma_node_is_hidden(security_card)) aroma_card_draw(security_card, window_id);
        if (security_fab && !aroma_node_is_hidden(security_fab)) aroma_fab_draw(security_fab, window_id);
        if (security_snackbar && !aroma_node_is_hidden(security_snackbar)) aroma_snackbar_draw(security_snackbar, window_id);
        if (display_progress && !aroma_node_is_hidden(display_progress)) aroma_progressbar_draw(display_progress, window_id);
        if (sound_slider && !aroma_node_is_hidden(sound_slider)) aroma_slider_draw(sound_slider, window_id);
        if (power_button && !aroma_node_is_hidden(power_button)) aroma_button_draw(power_button, window_id);
        if (power_dialog && !aroma_node_is_hidden(power_dialog)) aroma_dialog_draw(power_dialog, window_id);
        if (about_divider && !aroma_node_is_hidden(about_divider)) aroma_divider_draw(about_divider, window_id);
        if (about_icon_button && !aroma_node_is_hidden(about_icon_button)) aroma_iconbutton_draw(about_icon_button, window_id);
        if (about_tooltip && !aroma_node_is_hidden(about_tooltip)) aroma_tooltip_draw(about_tooltip, window_id);
        if (about_menu && !aroma_node_is_hidden(about_menu)) aroma_menu_draw(about_menu, window_id);
        if (network_dropdown) aroma_dropdown_render_overlays(window_id);
        if (debug_overlay && !aroma_node_is_hidden(debug_overlay)) aroma_debug_overlay_draw(debug_overlay, window_id);
        aroma_dirty_list_clear();
    }

    aroma_graphics_swap_buffers(window_id);
}

int main(void)
{
    if (!aroma_ui_init()) return 1;

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 12);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 12);

    AromaWindow* window = aroma_ui_create_window("Settings", 900, 600);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    content_root = create_container((AromaNode*)window, 216, 60, 668, 520);
    if (!content_root) content_root = (AromaNode*)window;

    section_network = create_container(content_root, 216, 60, 668, 520);
    section_display = create_container(content_root, 216, 60, 668, 520);
    section_sound = create_container(content_root, 216, 60, 668, 520);
    section_power = create_container(content_root, 216, 60, 668, 520);
    section_about = create_container(content_root, 216, 60, 668, 520);

    if (!section_network) section_network = content_root;
    if (!section_display) section_display = content_root;
    if (!section_sound) section_sound = content_root;
    if (!section_power) section_power = content_root;
    if (!section_about) section_about = content_root;

    sidebar = aroma_sidebar_create((AromaNode*)window, 16, 16, 180, 568, sidebar_items, 5);
    if (sidebar) {
        aroma_sidebar_set_font(sidebar, font);
        aroma_sidebar_set_on_select(sidebar, on_sidebar_select, NULL);
        aroma_sidebar_setup_events(sidebar, aroma_ui_request_redraw, NULL);
    }

    aroma_sidebar_set_font(sidebar, font);
    tabs = aroma_tabs_create(section_network, 216, 16, 668, 44, tab_items, 3);
    if (tabs) {
        aroma_tabs_set_font(tabs, font);
        aroma_tabs_set_on_change(tabs, on_tabs_change, NULL);
        aroma_tabs_setup_events(tabs, aroma_ui_request_redraw, NULL);
    }

    tab_general_container = create_container(section_network, 216, 70, 668, 500);
    tab_advanced_container = create_container(section_network, 216, 70, 668, 500);
    tab_security_container = create_container(section_network, 216, 70, 668, 500);

    header_label = aroma_label_create(content_root, "Network - General", 230, 76, LABEL_STYLE_TITLE_LARGE);
    if (header_label) {
        aroma_label_set_font(header_label, font);
    }

    wifi_label = aroma_label_create(tab_general_container, "Wi-Fi", 230, 120, LABEL_STYLE_BODY_LARGE);
    bluetooth_label = aroma_label_create(tab_general_container, "Bluetooth", 230, 180, LABEL_STYLE_BODY_LARGE);
    brightness_label = aroma_label_create(tab_advanced_container, "Bandwidth", 230, 140, LABEL_STYLE_BODY_LARGE);

    section_display_label = aroma_label_create(section_display, "Display settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_sound_label = aroma_label_create(section_sound, "Sound settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_power_label = aroma_label_create(section_power, "Power settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_about_label = aroma_label_create(section_about, "AromaUI Settings v1.0", 230, 120, LABEL_STYLE_BODY_LARGE);

    tab_general_label = aroma_label_create(tab_general_container, "General options", 230, 240, LABEL_STYLE_BODY_SMALL);
    tab_advanced_label = aroma_label_create(tab_advanced_container, "Advanced options", 230, 240, LABEL_STYLE_BODY_SMALL);
    tab_security_label = aroma_label_create(tab_security_container, "Security options", 230, 240, LABEL_STYLE_BODY_SMALL);

    if (wifi_label) aroma_label_set_font(wifi_label, font);
    if (bluetooth_label) aroma_label_set_font(bluetooth_label, font);
    if (brightness_label) aroma_label_set_font(brightness_label, font);
    if (section_display_label) aroma_label_set_font(section_display_label, font);
    if (section_sound_label) aroma_label_set_font(section_sound_label, font);
    if (section_power_label) aroma_label_set_font(section_power_label, font);
    if (section_about_label) aroma_label_set_font(section_about_label, font);
    if (tab_general_label) aroma_label_set_font(tab_general_label, font);
    if (tab_advanced_label) aroma_label_set_font(tab_advanced_label, font);
    if (tab_security_label) aroma_label_set_font(tab_security_label, font);

    wifi_switch = aroma_switch_create(tab_general_container, 520, 110, 52, 28, true);
    bluetooth_switch = aroma_switch_create(tab_general_container, 520, 170, 52, 28, false);
    if (wifi_switch) aroma_switch_setup_events(wifi_switch, aroma_ui_request_redraw, NULL);
    if (bluetooth_switch) aroma_switch_setup_events(bluetooth_switch, aroma_ui_request_redraw, NULL);

    brightness_slider = aroma_slider_create(tab_advanced_container, 230, 170, 280, 22, 0, 100, 65);
    if (brightness_slider) {
        aroma_slider_setup_events(brightness_slider, aroma_ui_request_redraw, NULL);
    }

    network_dropdown = aroma_dropdown_create(tab_general_container, 230, 270, 220, 32);
    if (network_dropdown) {
        aroma_dropdown_add_option(network_dropdown, "Office Wi-Fi");
        aroma_dropdown_add_option(network_dropdown, "Guest Network");
        aroma_dropdown_add_option(network_dropdown, "Mobile Hotspot");
        aroma_dropdown_set_font(network_dropdown, font);
        aroma_dropdown_setup_events(network_dropdown, aroma_ui_request_redraw, NULL);
    }

    device_name_textbox = aroma_textbox_create(tab_general_container, 230, 320, 220, 32);
    if (device_name_textbox) {
        aroma_textbox_set_font(device_name_textbox, font);
        aroma_textbox_set_placeholder(device_name_textbox, "Aroma Device");
        aroma_textbox_setup_events(device_name_textbox, aroma_ui_request_redraw, NULL, NULL);
    }

    auto_connect_checkbox = aroma_checkbox_create(tab_general_container, "Auto-connect", 230, 370, 220, 28);
    if (auto_connect_checkbox) {
        aroma_checkbox_set_font(auto_connect_checkbox, font);
        aroma_checkbox_set_checked(auto_connect_checkbox, true);
        aroma_checkbox_setup_events(auto_connect_checkbox, aroma_ui_request_redraw, NULL);
    }

    connect_button = aroma_button_create(tab_general_container, "Connect", 470, 310, 120, 36);
    if (connect_button) {
        aroma_button_set_font(connect_button, font);
        aroma_button_setup_events(connect_button, aroma_ui_request_redraw, NULL);
    }

    signal_progress = aroma_progressbar_create(tab_general_container, 230, 420, 360, 16, PROGRESS_TYPE_DETERMINATE);
    if (signal_progress) {
        aroma_progressbar_set_progress(signal_progress, 0.7f);
    }

    band_radio_24 = aroma_radiobutton_create(tab_advanced_container, "2.4 GHz", 230, 200, 160, 28, 1);
    band_radio_5 = aroma_radiobutton_create(tab_advanced_container, "5 GHz", 400, 200, 160, 28, 1);
    if (band_radio_24) {
        aroma_radiobutton_set_font(band_radio_24, font);
        aroma_radio_button_setup_events(band_radio_24, aroma_ui_request_redraw, NULL);
        aroma_radiobutton_set_selected(band_radio_24, true);
    }
    if (band_radio_5) {
        aroma_radiobutton_set_font(band_radio_5, font);
        aroma_radio_button_setup_events(band_radio_5, aroma_ui_request_redraw, NULL);
    }

    devices_list = aroma_listview_create(tab_advanced_container, 230, 260, 360, 140);
    if (devices_list) {
        aroma_listview_set_font(devices_list, font);
        aroma_listview_add_item(devices_list, "Printer", "192.168.1.20", NULL);
        aroma_listview_add_item(devices_list, "NAS", "192.168.1.30", NULL);
        aroma_listview_add_item(devices_list, "Laptop", "192.168.1.42", NULL);
    }

    security_chip = aroma_chip_create(tab_security_container, 230, 160, "Firewall", CHIP_TYPE_FILTER);
    if (security_chip) {
        aroma_chip_set_font(security_chip, font);
    }

    security_card = aroma_card_create(tab_security_container, 230, 210, 260, 110, CARD_TYPE_OUTLINED);
    security_fab = aroma_fab_create(tab_security_container, 520, 210, FAB_SIZE_NORMAL, "+");
    if (security_fab) {
        aroma_fab_set_font(security_fab, font);
        aroma_fab_set_text(security_fab, "Add Rule");
    }

    security_snackbar = aroma_snackbar_create(tab_security_container, "Rules updated", 2500);
    if (security_snackbar) {
        aroma_snackbar_set_font(security_snackbar, font);
        aroma_snackbar_set_action(security_snackbar, "UNDO", NULL, NULL);
        aroma_snackbar_show(security_snackbar);
    }

    display_progress = aroma_progressbar_create(section_display, 230, 170, 320, 16, PROGRESS_TYPE_DETERMINATE);
    if (display_progress) {
        aroma_progressbar_set_progress(display_progress, 0.35f);
    }

    sound_slider = aroma_slider_create(section_sound, 230, 170, 320, 22, 0, 100, 40);
    if (sound_slider) {
        aroma_slider_setup_events(sound_slider, aroma_ui_request_redraw, NULL);
    }

    power_button = aroma_button_create(section_power, "Sleep Now", 230, 170, 140, 36);
    if (power_button) {
        aroma_button_set_font(power_button, font);
        aroma_button_setup_events(power_button, aroma_ui_request_redraw, NULL);
    }

    power_dialog = aroma_dialog_create(section_power, "Power", "Turn off after 10 minutes?", 360, 180, DIALOG_TYPE_BASIC);
    if (power_dialog) {
        aroma_dialog_set_font(power_dialog, font);
        aroma_dialog_add_action(power_dialog, "OK", NULL, NULL);
        aroma_dialog_add_action(power_dialog, "Cancel", NULL, NULL);
        aroma_dialog_show(power_dialog);
    }

    about_divider = aroma_divider_create(section_about, 230, 150, 320, DIVIDER_ORIENTATION_HORIZONTAL);
    about_icon_button = aroma_iconbutton_create(section_about, "?", 230, 180, 36, 36);
    if (about_icon_button) {
        aroma_iconbutton_set_font(about_icon_button, font);
    }

    about_tooltip = aroma_tooltip_create(section_about, "AromaUI v1.0", 280, 180, TOOLTIP_POSITION_RIGHT);
    if (about_tooltip) {
        aroma_tooltip_set_font(about_tooltip, font);
        aroma_tooltip_show(about_tooltip, 0);
    }

    about_menu = aroma_menu_create(section_about, 230, 230);
    if (about_menu) {
        aroma_menu_set_font(about_menu, font);
        aroma_menu_add_item(about_menu, "Check Updates", NULL, NULL);
        aroma_menu_add_item(about_menu, "Licenses", NULL, NULL);
        aroma_menu_add_separator(about_menu);
        aroma_menu_add_item(about_menu, "About", NULL, NULL);
        aroma_menu_show(about_menu);
    }

    if (sidebar) {
        AromaNode* network_nodes[] = { section_network };
        AromaNode* display_nodes[] = { section_display };
        AromaNode* sound_nodes[] = { section_sound };
        AromaNode* power_nodes[] = { section_power };
        AromaNode* about_nodes[] = { section_about };

        aroma_sidebar_set_content(sidebar, 0, network_nodes, 1);
        aroma_sidebar_set_content(sidebar, 1, display_nodes, 1);
        aroma_sidebar_set_content(sidebar, 2, sound_nodes, 1);
        aroma_sidebar_set_content(sidebar, 3, power_nodes, 1);
        aroma_sidebar_set_content(sidebar, 4, about_nodes, 1);
    }

    if (tabs) {
        AromaNode* general_nodes[] = { tab_general_container };
        AromaNode* advanced_nodes[] = { tab_advanced_container };
        AromaNode* security_nodes[] = { tab_security_container };
        aroma_tabs_set_content(tabs, 0, general_nodes, 1);
        aroma_tabs_set_content(tabs, 1, advanced_nodes, 1);
        aroma_tabs_set_content(tabs, 2, security_nodes, 1);
    }

    debug_overlay = aroma_debug_overlay_create(content_root, 700, 520);
    if (debug_overlay) {
        aroma_debug_overlay_set_font(debug_overlay, font);
    }

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
    }

    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    return 0;
}
