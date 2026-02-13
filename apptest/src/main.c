/**
 * 
 * AromaUI minimal example
 * 
 */

#include "aroma.h"
#include "logo.h"
#include <stdio.h>
#include <unistd.h>

AromaNode* root_container = NULL;
AromaFont* font_md = NULL;

static bool on_btn_click(AromaNode* btn, void* data)
{
    char addresses[18];  // Array to store up to 10 device addresses
    char names[248];      // Array to store up to 10 device names
    
    int count = aroma_android_bt_get_paired(addresses, names, 10);
    
    if (count > 0) {
       char found[100];
       snprintf(found, sizeof(found), "Found %d paired Bluetooth devices:", count);
       aroma_ui_label((AromaNode*)root_container, found, 20, 20, LABEL_STYLE_LABEL_SMALL, font_md);
        for (int i = 0; i < count; i++) {
            aroma_ui_label((AromaNode*)root_container, names[i], 20, 20,
                LABEL_STYLE_LABEL_SMALL, font_md);
        }
    } else {
     //   LOGI("No paired Bluetooth devices found");
    }
    return true;
}

int main(int argc, char** argv)
{
    if (!aroma_ui_init()) {
        return 1;
    }

    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);

    AromaWindow* win = aroma_ui_create_window("AromaUI Showcase", 400, 800);
    aroma_window_set_fullscreen((AromaNode*)win, true);
    
    font_md = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 48);
    
    int w, h;
    aroma_window_get_size(win, &w, &h);
    
    root_container = aroma_ui_container(
        (AromaNode*)win, 0, 0, w, h,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    
    aroma_node_set_gap((AromaNode*)root_container, 40);
    
  
    aroma_ui_button((AromaNode*)root_container, "Scan", 20, 20, 100, 100,  on_btn_click, NULL, font_md);

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