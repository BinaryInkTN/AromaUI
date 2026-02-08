/**
 * 
 * AromaUI minimal example
 * 
 */

#include "aroma.h"
#include "logo.h"
#include <stdio.h>
#include <unistd.h>

static bool on_btn_click(AromaNode* btn, void* data)
{
    aroma_ui_android_intent(AROMA_INTENT_VIEW,
        "https://github.com/BinaryInkTN/AromaUI", NULL, NULL, 0);
    
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
    
    AromaFont* font_md = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 48);
    
    int w, h;
    aroma_window_get_size(win, &w, &h);
    
    AromaContainer* root_container = aroma_ui_container(
        (AromaNode*)win, 0, 0, w, h,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    
    aroma_node_set_gap((AromaNode*)root_container, 40);
    
    aroma_ui_image_mem((AromaNode*)root_container,
        leaf_png, leaf_png_len, 100, 50, 256, 256);
    
    aroma_ui_divider((AromaNode*)win, 0, 150, w, DIVIDER_ORIENTATION_HORIZONTAL);
    
    aroma_ui_label((AromaNode*)win, "Minimal App", 40, 50,
        LABEL_STYLE_LABEL_LARGE, font_md);
    
    AromaLabel* label = aroma_ui_create_label(
        (AromaNode*)root_container, "Hello, AromaUI!", 20, 20,
        LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font(label, font_md);
    
    AromaLabel* description = aroma_ui_create_label(
        (AromaNode*)root_container,
        "Press to visit our GitHub repository!", 20, 20,
        LABEL_STYLE_LABEL_SMALL);
    aroma_label_set_font((AromaNode*)description, font_md);
    
    status_label = (AromaNode*)description;
    
    aroma_ui_button((AromaNode*)root_container, "Let's go!", 20, 20,
        230, 100, on_btn_click, NULL, font_md);
    
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