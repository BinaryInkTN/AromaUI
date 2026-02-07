#include "aroma.h"
#include <stdio.h>

bool on_btn_click(AromaButton* btn, void* data) {
    (void)btn;
    (void)data;
    printf("Button Clicked!\n");
    return true;
}

int main(int argc, char** argv) {
    if (!aroma_ui_init()) return 1;
    
    AromaWindow* win = aroma_ui_create_window("test_app", 1920, 1080);
    // Attempt to load a default font

    AromaFont* font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 48);

    aroma_ui_prepare_font_for_window(0, font);
    aroma_event_set_root((AromaNode*)win);
    // Create a label
    AromaNode* label = aroma_ui_create_label((AromaNode*)win, "Welcome to AromaUI", 250, 100, LABEL_STYLE_LABEL_LARGE);
    if(font) aroma_label_set_font(label, font);
    
    // Create a button
    AromaButton* btn = aroma_ui_create_button(win, "Click Me", 300, 200, 300, 100);
    AromaNode* dropdown = aroma_dropdown_create((AromaNode*)win, 300, 350, 250, 100);
    aroma_dropdown_setup_events(dropdown, aroma_ui_request_redraw, NULL);
    aroma_dropdown_add_option(dropdown, "Option 1");
    aroma_dropdown_add_option(dropdown, "Option 2");
    aroma_dropdown_set_font(dropdown, font);
    
    // Button font settings usage omitted as it is likely internal or handled if font is global theme
    aroma_ui_on_button_click(btn, on_btn_click, NULL);
    aroma_button_set_font((AromaNode*)btn, font);
    
    while(aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
        aroma_node_invalidate_tree((AromaNode*)win);
    }
    
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>
void android_main(struct android_app* state) {
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif
