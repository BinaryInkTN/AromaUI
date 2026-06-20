#include <aroma.h>
#include <unistd.h>
#include <aroma_incense_loader.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <stdio.h>
#include <stdlib.h>

void on_button_click(void *userdata)
{
    static int count = 0;
    count++;
    printf("[Callback] Button clicked! Count: %d\n", count);
}

void on_fab_click(void *userdata)
{
    printf("[Callback] FAB clicked!\n");
}

void on_icon_button_click(void *userdata)
{
    printf("[Callback] Icon button clicked!\n");
}

void on_checkbox_change(bool checked, void *userdata)
{
    printf("[Callback] Checkbox changed: %s\n", checked ? "true" : "false");
}

void on_switch_change(AromaNode *node, void *userdata)
{
    printf("[Callback] Switch toggled!\n");
}

void on_slider_change(AromaNode *node, void *userdata)
{
    printf("[Callback] Slider value changed!\n");
}

void on_textbox_change(AromaNode *node, const char *text, void *userdata)
{
    printf("[Callback] Textbox text: %s\n", text);
}

void on_dropdown_change(int index, const char *option, void *userdata)
{
    printf("[Callback] Dropdown selected: %s (index: %d)\n", option, index);
}

void on_listview_select(int index, void *userdata)
{
    printf("[Callback] List item selected: %d\n", index);
}

void on_menu_item_click(void *userdata)
{
    printf("[Callback] Menu item clicked!\n");
}

void on_tab_change(AromaNode *node, int index, void *userdata)
{
    printf("[Callback] Tab changed to: %d\n", index);
}

void on_sidebar_select(AromaNode *node, int index, void *userdata)
{
    printf("[Callback] Sidebar item selected: %d\n", index);
}

void on_radio_click(void *userdata)
{
    printf("[Callback] Radio button clicked!\n");
}

int main()
{
    printf("=== AromaUI Complete Test Suite ===\n");
    printf("Initializing UI system...\n");
    aroma_ui_init();
    
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    
    printf("Registering callbacks...\n");
    IncenseRegisterCallback("onButtonClick", INCENSE_CALLBACK_VOID_PTR, on_button_click, NULL);
    IncenseRegisterCallback("onFABClick", INCENSE_CALLBACK_VOID_PTR, on_fab_click, NULL);
    IncenseRegisterCallback("onIconButtonClick", INCENSE_CALLBACK_VOID_PTR, on_icon_button_click, NULL);
    IncenseRegisterCallback("onCheckboxChange", INCENSE_CALLBACK_BOOL_BOOL_PTR, on_checkbox_change, NULL);
    IncenseRegisterCallback("onSwitchChange", INCENSE_CALLBACK_BOOL_PTR, on_switch_change, NULL);
    IncenseRegisterCallback("onSliderChange", INCENSE_CALLBACK_BOOL_PTR, on_slider_change, NULL);
    IncenseRegisterCallback("onTextboxChange", INCENSE_CALLBACK_NODE_STRING_PTR, on_textbox_change, NULL);
    IncenseRegisterCallback("onDropdownChange", INCENSE_CALLBACK_INT_STRING_PTR, on_dropdown_change, NULL);
    IncenseRegisterCallback("onListSelect", INCENSE_CALLBACK_INT_PTR, on_listview_select, NULL);
    IncenseRegisterCallback("onMenuItemClick", INCENSE_CALLBACK_VOID_PTR, on_menu_item_click, NULL);
    IncenseRegisterCallback("onTabChanged", INCENSE_CALLBACK_NODE_INT_PTR, on_tab_change, NULL);
    IncenseRegisterCallback("onSidebarSelect", INCENSE_CALLBACK_NODE_INT_PTR, on_sidebar_select, NULL);
    IncenseRegisterCallback("onRadioClick", INCENSE_CALLBACK_VOID_PTR, on_radio_click, NULL);
    
    printf("Loading UI from test.aroma...\n");
    AromaWindow *window = IncenseLoadFile("../ui.aroma", text_font, icon_font);
    if (!window) {
        printf("Failed to load test.aroma\n");
        aroma_font_destroy(text_font);
        aroma_font_destroy(icon_font);
        aroma_ui_shutdown();
        return 1;
    }
    
    printf("Starting main loop...\n");
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

    printf("Shutting down...\n");
    aroma_font_destroy(text_font);
    aroma_font_destroy(icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    printf("Done!\n");
    return 0;
}