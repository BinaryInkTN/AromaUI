/**
 * 
 * AromaUI Robot Control Application
 * 
 */

#include "aroma.h"
#include "logo.h"
#include "aroma_android.h"
#include "widgets/aroma_dialog.h"
#include "widgets/aroma_listview.h"
#include "widgets/aroma_tabs.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

static AromaNode* g_status_label = NULL;
static AromaNode* g_status_chip = NULL;
static bool g_connected = false;
static char g_connected_device[248] = {0};
static AromaNode* g_device_list_view = NULL;

static void update_connection_status(bool connected, const char* name) {
    g_connected = connected;
    if (connected) {
        if (name) strncpy(g_connected_device, name, 247);
        if (g_status_chip) {
            aroma_chip_set_text(g_status_chip, g_connected_device);
            aroma_chip_set_selected(g_status_chip, true);
        }
        if (g_status_label) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Status: Connected to %s", g_connected_device);
            aroma_label_set_text(g_status_label, buf);
        }
        aroma_android_toast("Connected to robot", false);
    } else {
        if (g_status_chip) {
             aroma_chip_set_text(g_status_chip, "Not Connected");
             aroma_chip_set_selected(g_status_chip, false);
        }
        if (g_status_label) {
             aroma_label_set_text(g_status_label, "Status: Disconnected");
        }
    }
}

static bool on_control_click(AromaButton* btn, void* ud) {
    const char* cmd = (const char*)ud;
    (void)btn;
    
    if (!cmd) return false;
    
    if (g_connected) {
        aroma_android_bt_send(cmd, strlen(cmd));
        aroma_android_bt_send("\n", 1);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "Status: Sending %s...", cmd);
        if (g_status_label) aroma_label_set_text(g_status_label, buf);
    } else {
        aroma_android_toast("Not connected to any device", false);
    }
   
    return true;
}

// Helper to hold device list data
typedef struct {
    char addrs[10][18];
    char names[10][248];
    int count;
} DeviceListData;

static DeviceListData g_device_list;

static void on_device_selected(int index, void* user_data) {
    (void)user_data;
    if (index >= 0 && index < g_device_list.count) {
        aroma_android_toast("Connecting...", false);
        if (aroma_android_bt_connect(g_device_list.addrs[index])) {
             update_connection_status(true, g_device_list.names[index]);
        } else {
             aroma_android_toast("Connection failed", false);
        }
    }
}

static void refresh_devices() {
    if (!g_device_list_view) return;
    
    aroma_listview_clear(g_device_list_view);
    
    g_device_list.count = aroma_android_bt_get_paired(g_device_list.addrs, g_device_list.names, 10);
    
    if (g_device_list.count == 0) {
        // Mock data for testing
        g_device_list.count = 3;
        strcpy(g_device_list.names[0], "Simulation Bot 1");
        strcpy(g_device_list.addrs[0], "AA:BB:CC:00:11:22");
        strcpy(g_device_list.names[1], "Living Room Unit");
        strcpy(g_device_list.addrs[1], "11:22:33:44:55:66");
        strcpy(g_device_list.names[2], "Test Dev");
        strcpy(g_device_list.addrs[2], "XX:YY:ZZ:WW:VV:UU");
    }

    for(int i=0; i<g_device_list.count; i++) {
        const char* name = g_device_list.names[i];
        if (!name || strlen(name) == 0) name = g_device_list.addrs[i];
        aroma_listview_add_item_with_icon(g_device_list_view, name, g_device_list.addrs[i], AROMA_ICON_BLUETOOTH, NULL);
    }
    
    aroma_android_toast("Device list refreshed", false);
}

static bool on_scan_click(AromaButton* btn, void* ud) {
    (void)btn; (void)ud;
    // Check permission first
    if (!aroma_android_check_permission("android.permission.BLUETOOTH_CONNECT")) {
        aroma_android_request_permission("android.permission.BLUETOOTH_CONNECT");
        return true;
    }
    refresh_devices();
    return true;
}

int main(int argc, char** argv)
{
    if (!aroma_ui_init()) {
        return 1;
    }

    AromaTheme theme = aroma_theme_create_material_orange_dark();
    aroma_ui_set_theme(&theme);

    AromaWindow* win = aroma_ui_create_window("Robot Control", 400, 800);
#ifdef __ANDROID__
    aroma_window_set_fullscreen((AromaNode*)win, true);
#endif
    
    // Increased font sizes
    AromaFont* font_md = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 56);
    AromaFont* font_button = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 32);
    AromaFont* icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 80);
    
    int w, h;
    aroma_window_get_size((AromaNode*)win, &w, &h);
 
    // ---- Tabs Setup ----
    // Increased tab height from 60 to 120
    int tab_height = 120;
    const char* tab_labels[] = {"Connect", "Control"};
    const char* tab_icons[] = {AROMA_ICON_SEARCH, AROMA_ICON_GAMEPAD};
    
    AromaNode* tabs = aroma_ui_tabs_with_icons(
        (AromaNode*)win, 
        0, 0, w, tab_height, 
        tab_labels, tab_icons, 2, 
        NULL, NULL, 
        font_md, icon_font
    );
    
    int content_h = h - tab_height;

    // ---- Tab 0: Connection Page ----
    AromaNode* page_connect = aroma_ui_container((AromaNode*)win, 0, tab_height, w, content_h, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap(page_connect, 30);
    
    aroma_ui_create_label(page_connect, "Available Devices", 20, 20, LABEL_STYLE_LABEL_LARGE);
    
    // Adjusted ListView sizing
    int list_h = content_h / 2; // Use half of the remaining space
    
    // Container for list to manage layout better
    AromaNode* list_container = aroma_ui_container(page_connect, 0, 0, w - 80, list_h, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    
    g_device_list_view = aroma_listview_create(list_container, 0, 0, w - 80, list_h);
    if (font_md) aroma_listview_set_font(g_device_list_view, font_md);
    if (icon_font) aroma_listview_set_icon_font(g_device_list_view, icon_font);
    
    AromaButton* scan_btn = aroma_ui_create_button((AromaWindow*)page_connect, "Scan / Refresh", 0, 0, 240, 80);
    aroma_ui_on_button_click(scan_btn, (bool (*)(AromaButton*, void*))on_scan_click, NULL);
    if (font_button) aroma_button_set_font((AromaNode*)scan_btn, font_button);

    // ---- Tab 1: Control Page ----
    AromaNode* page_control = aroma_ui_container((AromaNode*)win, 0, tab_height, w, content_h, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_hidden(page_control, true); // Start hidden
    
    aroma_node_set_gap(page_control, 40);
    
    g_status_chip = aroma_ui_chip_with_icon(page_control, "Not Connected", 0, 0, CHIP_TYPE_FILTER, NULL, NULL, font_md, AROMA_ICON_BLUETOOTH, icon_font);
    
    AromaNode* remote_grid = aroma_ui_container(page_control, 0, 0, w-60, w-60, AROMA_LAYOUT_MODE_GRID, 0, 0, 0);
    aroma_node_set_grid_cols(remote_grid, 3);
    aroma_node_set_grid_rows(remote_grid, 3);
    aroma_node_set_gap(remote_grid, 12);

    const char* labels[] = {"Forward","Backward","Left","Right","RotateL","RotateR","Speed+","Speed-","Stop"};
    const char* cmds[] = {"FWD","BWD","LEFT","RIGHT","RL","RR","SPD+","SPD-","STOP"};
    const char* icons[] = {AROMA_ICON_ARROW_FORWARD, AROMA_ICON_ARROW_BACK, AROMA_ICON_ARROW_LEFT, AROMA_ICON_ARROW_RIGHT, AROMA_ICON_ROTATE_LEFT, AROMA_ICON_ROTATE_RIGHT, AROMA_ICON_ARROW_UPWARD, AROMA_ICON_ARROW_DOWNWARD, AROMA_ICON_STOP};
    
    for (int i = 0; i < 9; i++) {
        AromaButton* b = aroma_ui_create_button((AromaWindow*)remote_grid, labels[i], 0, 0, 100, 100); 
        aroma_ui_on_button_click(b, on_control_click, (void*)cmds[i]);
        if (font_button) aroma_button_set_font((AromaNode*)b, font_button);
        if (icon_font) aroma_button_set_icon((AromaNode*)b, icons[i], icon_font);
    }

    g_status_label = aroma_ui_create_label(page_control, "Status: Idle", 0, 0, LABEL_STYLE_LABEL_SMALL);

    // ---- Tab Switching Logic ----
    AromaNode* pages[] = {page_connect, page_control};
    aroma_tabs_set_content(tabs, 0, pages, 1); 
    aroma_tabs_set_content(tabs, 1, &pages[1], 1);
    
    // Initial refresh
    refresh_devices();
    
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
        usleep(16000);
    }
    
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>
void android_main(struct android_app* state)
{
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif
