#include <aroma.h>

#include <unistd.h>
#include <stdio.h>

static AromaFont* font = NULL;
static AromaSidebar* sidebar = NULL;
static AromaTabs* tabs = NULL;
static AromaContainer* content_root = NULL;
static AromaContainer* section_network = NULL;
static AromaContainer* section_display = NULL;
static AromaContainer* section_sound = NULL;
static AromaContainer* section_power = NULL;
static AromaContainer* section_about = NULL;
static AromaContainer* tab_general_container = NULL;
static AromaContainer* tab_advanced_container = NULL;
static AromaContainer* tab_security_container = NULL;
static AromaLabel* header_label = NULL;
static AromaLabel* wifi_label = NULL;
static AromaSwitch* wifi_switch = NULL;
static AromaLabel* bluetooth_label = NULL;
static AromaSwitch* bluetooth_switch = NULL;
static AromaLabel* brightness_label = NULL;
static AromaSlider* brightness_slider = NULL;   
static AromaDebugOverlay* debug_overlay = NULL;
static AromaLabel* section_display_label = NULL;
static AromaLabel* section_sound_label = NULL;
static AromaLabel* section_power_label = NULL;
static AromaLabel* section_about_label = NULL;
static AromaLabel* tab_general_label = NULL;
static AromaLabel* tab_advanced_label = NULL;
static AromaLabel* tab_security_label = NULL;
static AromaDropdown* network_dropdown = NULL;
static AromaTextbox* device_name_textbox = NULL;
static AromaCheckbox* auto_connect_checkbox = NULL;
static AromaButton* connect_button = NULL;
static AromaProgressBar* signal_progress = NULL;
static AromaRadioButton* band_radio_24 = NULL;
static AromaRadioButton* band_radio_5 = NULL;
static AromaListView* devices_list = NULL;
static AromaChip* security_chip = NULL;
static AromaCard* security_card = NULL;
static AromaFAB* security_fab = NULL;
static AromaSnackbar* security_snackbar = NULL;
static AromaDialog* power_dialog = NULL;
static AromaMenu* about_menu = NULL;
static AromaTooltip* about_tooltip = NULL;
static AromaIconButton* about_icon_button = NULL;
static AromaDivider* about_divider = NULL;
static AromaProgressBar* display_progress = NULL;
static AromaSlider* sound_slider = NULL;
static AromaButton* power_button = NULL;

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
    aroma_label_set_text((AromaNode*)header_label, sidebar_items[index]);
    aroma_ui_request_redraw(NULL);
}

static void on_tabs_change(AromaNode* node, int index, void* user_data)
{
    (void)node;
    (void)user_data;
    if (!header_label) return;

    int sidebar_index = sidebar ? aroma_sidebar_get_selected((AromaNode*)sidebar) : -1;
    if (sidebar_index < 0 || sidebar_index >= (int)(sizeof(sidebar_items) / sizeof(sidebar_items[0]))) {
        sidebar_index = 0;
    }

    char title[64];
    snprintf(title, sizeof(title), "%s - %s", sidebar_items[sidebar_index], tab_items[index]);
    aroma_label_set_text((AromaNode*)header_label, title);
    aroma_ui_request_redraw(NULL);
}

static void on_snackbar_undo(void* user_data)
{
    (void)user_data;
    if (header_label) {
        aroma_label_set_text((AromaNode*)header_label, "Undo pressed");
        aroma_ui_request_redraw(NULL);
    }
}

static void window_update_callback(size_t window_id, void* data)
{
    (void)data;
    if (!aroma_ui_consume_redraw()) return;

    aroma_ui_begin_frame(window_id);

    AromaTheme theme = aroma_theme_get_global();
    aroma_ui_render_dirty_window(window_id, theme.colors.background);
    if (network_dropdown) aroma_dropdown_render_overlays(window_id);

    aroma_ui_end_frame(window_id);
    aroma_graphics_swap_buffers(window_id);
}

int main(void)
{
    if (!aroma_ui_init()) return 1;

    AromaTheme preset = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
    aroma_ui_set_theme(&preset);

    font = aroma_font_create_from_memory(aroma_ubuntu_ttf, 
                                        aroma_ubuntu_ttf_len, 14);
    if (!font) {
        LOG_ERROR("Failed to load embedded font");
        aroma_ui_shutdown();
        return 1;
    }

    AromaWindow* window = aroma_ui_create_window("Settings", 900, 600);
    if (!window) return 1;

    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);

    content_root = (AromaContainer*)aroma_container_create((AromaNode*)window, 216, 60, 668, 520);
    AromaNode* content_root_node = content_root ? (AromaNode*)content_root : (AromaNode*)window;

    section_network = (AromaContainer*)aroma_container_create(content_root_node, 216, 60, 668, 520);
    section_display = (AromaContainer*)aroma_container_create(content_root_node, 216, 60, 668, 520);
    section_sound = (AromaContainer*)aroma_container_create(content_root_node, 216, 60, 668, 520);
    section_power = (AromaContainer*)aroma_container_create(content_root_node, 216, 60, 668, 520);
    section_about = (AromaContainer*)aroma_container_create(content_root_node, 216, 60, 668, 520);

    AromaNode* section_network_node = section_network ? (AromaNode*)section_network : content_root_node;
    AromaNode* section_display_node = section_display ? (AromaNode*)section_display : content_root_node;
    AromaNode* section_sound_node = section_sound ? (AromaNode*)section_sound : content_root_node;
    AromaNode* section_power_node = section_power ? (AromaNode*)section_power : content_root_node;
    AromaNode* section_about_node = section_about ? (AromaNode*)section_about : content_root_node;

    sidebar = (AromaSidebar*)aroma_sidebar_create((AromaNode*)window, 16, 16, 180, 568, sidebar_items, 5);
    if (sidebar) {
        aroma_sidebar_set_font((AromaNode*)sidebar, font);
        aroma_sidebar_set_on_select((AromaNode*)sidebar, on_sidebar_select, NULL);
        aroma_sidebar_setup_events((AromaNode*)sidebar, aroma_ui_request_redraw, NULL);
    }

    aroma_sidebar_set_font((AromaNode*)sidebar, font);
    tabs = (AromaTabs*)aroma_tabs_create(section_network_node, 216, 16, 668, 44, tab_items, 3);
    if (tabs) {
        aroma_tabs_set_font((AromaNode*)tabs, font);
        aroma_tabs_set_on_change((AromaNode*)tabs, on_tabs_change, NULL);
        aroma_tabs_setup_events((AromaNode*)tabs, aroma_ui_request_redraw, NULL);
    }

    tab_general_container = (AromaContainer*)aroma_container_create(section_network_node, 216, 70, 668, 500);
    tab_advanced_container = (AromaContainer*)aroma_container_create(section_network_node, 216, 70, 668, 500);
    tab_security_container = (AromaContainer*)aroma_container_create(section_network_node, 216, 70, 668, 500);

    AromaNode* tab_general_node = tab_general_container ? (AromaNode*)tab_general_container : section_network_node;
    AromaNode* tab_advanced_node = tab_advanced_container ? (AromaNode*)tab_advanced_container : section_network_node;
    AromaNode* tab_security_node = tab_security_container ? (AromaNode*)tab_security_container : section_network_node;

    header_label = (AromaLabel*)aroma_label_create(content_root_node, "Network - General", 230, 76, LABEL_STYLE_TITLE_LARGE);
    if (header_label) {
        aroma_label_set_font((AromaNode*)header_label, font);
    }

    wifi_label = (AromaLabel*)aroma_label_create(tab_general_node, "Wi-Fi", 230, 120, LABEL_STYLE_BODY_LARGE);
    bluetooth_label = (AromaLabel*)aroma_label_create(tab_general_node, "Bluetooth", 230, 180, LABEL_STYLE_BODY_LARGE);
    brightness_label = (AromaLabel*)aroma_label_create(tab_advanced_node, "Bandwidth", 230, 140, LABEL_STYLE_BODY_LARGE);

    section_display_label = (AromaLabel*)aroma_label_create(section_display_node, "Display settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_sound_label = (AromaLabel*)aroma_label_create(section_sound_node, "Sound settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_power_label = (AromaLabel*)aroma_label_create(section_power_node, "Power settings", 230, 120, LABEL_STYLE_BODY_LARGE);
    section_about_label = (AromaLabel*)aroma_label_create(section_about_node, "AromaUI Settings v1.0", 230, 120, LABEL_STYLE_BODY_LARGE);

    tab_general_label = (AromaLabel*)aroma_label_create(tab_general_node, "General options", 230, 240, LABEL_STYLE_BODY_SMALL);
    tab_advanced_label = (AromaLabel*)aroma_label_create(tab_advanced_node, "Advanced options", 230, 240, LABEL_STYLE_BODY_SMALL);
    tab_security_label = (AromaLabel*)aroma_label_create(tab_security_node, "Security options", 230, 240, LABEL_STYLE_BODY_SMALL);

    if (wifi_label) aroma_label_set_font((AromaNode*)wifi_label, font);
    if (bluetooth_label) aroma_label_set_font((AromaNode*)bluetooth_label, font);
    if (brightness_label) aroma_label_set_font((AromaNode*)brightness_label, font);
    if (section_display_label) aroma_label_set_font((AromaNode*)section_display_label, font);
    if (section_sound_label) aroma_label_set_font((AromaNode*)section_sound_label, font);
    if (section_power_label) aroma_label_set_font((AromaNode*)section_power_label, font);
    if (section_about_label) aroma_label_set_font((AromaNode*)section_about_label, font);
    if (tab_general_label) aroma_label_set_font((AromaNode*)tab_general_label, font);
    if (tab_advanced_label) aroma_label_set_font((AromaNode*)tab_advanced_label, font);
    if (tab_security_label) aroma_label_set_font((AromaNode*)tab_security_label, font);

    wifi_switch = (AromaSwitch*)aroma_switch_create(tab_general_node, 520, 110, 52, 28, true);
    bluetooth_switch = (AromaSwitch*)aroma_switch_create(tab_general_node, 520, 170, 52, 28, false);
    if (wifi_switch) aroma_switch_setup_events((AromaNode*)wifi_switch, aroma_ui_request_redraw, NULL);
    if (bluetooth_switch) aroma_switch_setup_events((AromaNode*)bluetooth_switch, aroma_ui_request_redraw, NULL);

    brightness_slider = (AromaSlider*)aroma_slider_create(tab_advanced_node, 230, 170, 280, 22, 0, 100, 65);
    if (brightness_slider) {
        aroma_slider_setup_events((AromaNode*)brightness_slider, aroma_ui_request_redraw, NULL);
    }

    network_dropdown = (AromaDropdown*)aroma_dropdown_create(tab_general_node, 230, 270, 220, 32);
    if (network_dropdown) {
        aroma_dropdown_add_option((AromaNode*)network_dropdown, "Office Wi-Fi");
        aroma_dropdown_add_option((AromaNode*)network_dropdown, "Guest Network");
        aroma_dropdown_add_option((AromaNode*)network_dropdown, "Mobile Hotspot");
        aroma_dropdown_set_font((AromaNode*)network_dropdown, font);
        aroma_dropdown_setup_events((AromaNode*)network_dropdown, aroma_ui_request_redraw, NULL);
    }

    device_name_textbox = (AromaTextbox*)aroma_textbox_create(tab_general_node, 230, 320, 220, 32);
    if (device_name_textbox) {
        aroma_textbox_set_font((AromaNode*)device_name_textbox, font);
        aroma_textbox_set_placeholder((AromaNode*)device_name_textbox, "Aroma Device");
        aroma_textbox_setup_events((AromaNode*)device_name_textbox, aroma_ui_request_redraw, NULL, NULL);
    }

    auto_connect_checkbox = (AromaCheckbox*)aroma_checkbox_create(tab_general_node, "Auto-connect", 230, 370, 220, 28);
    if (auto_connect_checkbox) {
        aroma_checkbox_set_font((AromaNode*)auto_connect_checkbox, font);
        aroma_checkbox_set_checked((AromaNode*)auto_connect_checkbox, true);
        aroma_checkbox_setup_events((AromaNode*)auto_connect_checkbox, aroma_ui_request_redraw, NULL);
    }

    connect_button = (AromaButton*)aroma_button_create(tab_general_node, "Connect", 470, 310, 120, 36);
    if (connect_button) {
        aroma_button_set_font((AromaNode*)connect_button, font);
        aroma_button_setup_events((AromaNode*)connect_button, aroma_ui_request_redraw, NULL);
    }

    signal_progress = (AromaProgressBar*)aroma_progressbar_create(tab_general_node, 230, 420, 360, 16, PROGRESS_TYPE_DETERMINATE);
    if (signal_progress) {
        aroma_progressbar_set_progress((AromaNode*)signal_progress, 0.7f);
    }

    band_radio_24 = (AromaRadioButton*)aroma_radiobutton_create(tab_advanced_node, "2.4 GHz", 230, 200, 160, 28, 1);
    band_radio_5 = (AromaRadioButton*)aroma_radiobutton_create(tab_advanced_node, "5 GHz", 400, 200, 160, 28, 1);
    if (band_radio_24) {
        aroma_radiobutton_set_font((AromaNode*)band_radio_24, font);
        aroma_radio_button_setup_events((AromaNode*)band_radio_24, aroma_ui_request_redraw, NULL);
        aroma_radiobutton_set_selected((AromaNode*)band_radio_24, true);
    }
    if (band_radio_5) {
        aroma_radiobutton_set_font((AromaNode*)band_radio_5, font);
        aroma_radio_button_setup_events((AromaNode*)band_radio_5, aroma_ui_request_redraw, NULL);
    }

    devices_list = (AromaListView*)aroma_listview_create(tab_advanced_node, 230, 260, 360, 140);
    if (devices_list) {
        aroma_listview_set_font((AromaNode*)devices_list, font);
        aroma_listview_add_item((AromaNode*)devices_list, "Printer", "192.168.1.20", NULL);
        aroma_listview_add_item((AromaNode*)devices_list, "NAS", "192.168.1.30", NULL);
        aroma_listview_add_item((AromaNode*)devices_list, "Laptop", "192.168.1.42", NULL);
    }

    security_chip = (AromaChip*)aroma_chip_create(tab_security_node, 230, 160, "Firewall", CHIP_TYPE_FILTER);
    if (security_chip) {
        aroma_chip_set_font((AromaNode*)security_chip, font);
    }

    security_card = (AromaCard*)aroma_card_create(tab_security_node, 230, 210, 260, 110, CARD_TYPE_OUTLINED);
    security_fab = (AromaFAB*)aroma_fab_create(tab_security_node, 520, 210, FAB_SIZE_NORMAL, "+");
    if (security_fab) {
        aroma_fab_set_font((AromaNode*)security_fab, font);
        aroma_fab_set_text((AromaNode*)security_fab, "Add Rule");
    }

    security_snackbar = (AromaSnackbar*)aroma_snackbar_create(tab_security_node, "Rules updated", 2500);
    if (security_snackbar) {
        aroma_snackbar_set_font((AromaNode*)security_snackbar, font);
        aroma_snackbar_set_action((AromaNode*)security_snackbar, "UNDO", on_snackbar_undo, NULL);
        aroma_snackbar_show((AromaNode*)security_snackbar);
    }

    display_progress = (AromaProgressBar*)aroma_progressbar_create(section_display_node, 230, 170, 320, 16, PROGRESS_TYPE_DETERMINATE);
    if (display_progress) {
        aroma_progressbar_set_progress((AromaNode*)display_progress, 0.35f);
    }

    sound_slider = (AromaSlider*)aroma_slider_create(section_sound_node, 230, 170, 320, 22, 0, 100, 40);
    if (sound_slider) {
        aroma_slider_setup_events((AromaNode*)sound_slider, aroma_ui_request_redraw, NULL);
    }

    power_button = (AromaButton*)aroma_button_create(section_power_node, "Sleep Now", 230, 170, 140, 36);
    if (power_button) {
        aroma_button_set_font((AromaNode*)power_button, font);
        aroma_button_setup_events((AromaNode*)power_button, aroma_ui_request_redraw, NULL);
    }

    power_dialog = (AromaDialog*)aroma_dialog_create(section_power_node, "Power", "Turn off after 10 minutes?", 360, 180, DIALOG_TYPE_BASIC);
    if (power_dialog) {
        aroma_dialog_set_font((AromaNode*)power_dialog, font);
        aroma_dialog_add_action((AromaNode*)power_dialog, "OK", NULL, NULL);
        aroma_dialog_add_action((AromaNode*)power_dialog, "Cancel", NULL, NULL);
        aroma_dialog_show((AromaNode*)power_dialog);
    }

    about_divider = (AromaDivider*)aroma_divider_create(section_about_node, 230, 150, 320, DIVIDER_ORIENTATION_HORIZONTAL);
    about_icon_button = (AromaIconButton*)aroma_iconbutton_create(section_about_node, "?", 230, 180, 36, 36);
    if (about_icon_button) {
        aroma_iconbutton_set_font((AromaNode*)about_icon_button, font);
    }

    about_tooltip = (AromaTooltip*)aroma_tooltip_create(section_about_node, "AromaUI v1.0", 280, 180, TOOLTIP_POSITION_RIGHT);
    if (about_tooltip) {
        aroma_tooltip_set_font((AromaNode*)about_tooltip, font);
        aroma_tooltip_show((AromaNode*)about_tooltip, 0);
    }

    about_menu = (AromaMenu*)aroma_menu_create(section_about_node, 230, 230);
    if (about_menu) {
        aroma_menu_set_font((AromaNode*)about_menu, font);
        aroma_menu_add_item((AromaNode*)about_menu, "Check Updates", NULL, NULL);
        aroma_menu_add_item((AromaNode*)about_menu, "Licenses", NULL, NULL);
        aroma_menu_add_separator((AromaNode*)about_menu);
        aroma_menu_add_item((AromaNode*)about_menu, "About", NULL, NULL);
        aroma_menu_show((AromaNode*)about_menu);
    }

    if (sidebar) {
        AromaNode* network_nodes[] = { section_network_node };
        AromaNode* display_nodes[] = { section_display_node };
        AromaNode* sound_nodes[] = { section_sound_node };
        AromaNode* power_nodes[] = { section_power_node };
        AromaNode* about_nodes[] = { section_about_node };

        aroma_sidebar_set_content((AromaNode*)sidebar, 0, network_nodes, 1);
        aroma_sidebar_set_content((AromaNode*)sidebar, 1, display_nodes, 1);
        aroma_sidebar_set_content((AromaNode*)sidebar, 2, sound_nodes, 1);
        aroma_sidebar_set_content((AromaNode*)sidebar, 3, power_nodes, 1);
        aroma_sidebar_set_content((AromaNode*)sidebar, 4, about_nodes, 1);
        aroma_sidebar_set_font((AromaNode*)sidebar, font);
    }

    if (tabs) {
        AromaNode* general_nodes[] = { tab_general_node };
        AromaNode* advanced_nodes[] = { tab_advanced_node };
        AromaNode* security_nodes[] = { tab_security_node };
        aroma_tabs_set_content((AromaNode*)tabs, 0, general_nodes, 1);
        aroma_tabs_set_content((AromaNode*)tabs, 1, advanced_nodes, 1);
        aroma_tabs_set_content((AromaNode*)tabs, 2, security_nodes, 1);
    }

    debug_overlay = (AromaDebugOverlay*)aroma_debug_overlay_create(content_root_node, 700, 300);
    if (debug_overlay) {
        aroma_debug_overlay_set_font((AromaNode*)debug_overlay, font);
    }

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
    }

    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    return 0;
}
