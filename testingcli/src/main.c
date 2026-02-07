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
    
    AromaWindow* win = aroma_ui_create_window("testingcli", 800, 480);
    
    // Attempt to load a default font
#ifdef __ANDROID__
    AromaFont* font = aroma_ui_load_font("/system/fonts/Roboto-Regular.ttf", 18);
#else
    AromaFont* font = aroma_ui_load_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18);
#endif 

    // Create a label
    AromaNode* label = aroma_ui_create_label((AromaNode*)win, "Welcome to AromaUI", 250, 100, LABEL_STYLE_LABEL_LARGE);
    if(font) aroma_label_set_font(label, font);
    
    // Create a button
    AromaButton* btn = aroma_ui_create_button(win, "Click Me", 300, 200, 200, 50);
    aroma_ui_on_button_click(btn, on_btn_click, NULL);
    aroma_button_set_font((AromaNode*) btn, font);

    while(aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);

        aroma_node_invalidate_tree((AromaNode*) win);
    
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
