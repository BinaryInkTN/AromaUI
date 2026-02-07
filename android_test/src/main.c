#include "aroma.h"
#include <stdio.h>
#include "logo_data.h"

AromaSnackbar*snackbar = NULL;



bool on_btn_click(AromaButton* btn, void* data) {
    (void)btn;
    (void)data;
    if (snackbar) {
        aroma_snackbar_show(snackbar);
    }

    return true;
}

void on_undo(void* user_data) {
    (void)user_data;
    printf("Undo action triggered!\n");
    //aroma_node_set_hidden((AromaNode*)snackbar, true);
}

int main(int argc, char** argv) {
    if (!aroma_ui_init()) return 1;
    
    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);
    AromaWindow* win = aroma_ui_create_window("test_app", 400, 800);
    // Attempt to load a default font
    aroma_window_set_fullscreen((AromaNode*)win, true);

    AromaFont* font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 48);

    aroma_ui_prepare_font_for_window(0, font);
    aroma_event_set_root((AromaNode*)win);
    // Create a label
    char title[64];
    int w, h;
   
   


    const char *tab_labels[] = {"Tab 1", "Tab 2", "Tab 3"};

    aroma_window_get_size((AromaNode*)win, &w, &h);
    
    AromaNode* tabs = aroma_tabs_create((AromaNode*)win, 0, 0, w, 120, tab_labels, 3);
    aroma_tabs_set_font(tabs, font);
    aroma_tabs_setup_events(tabs, aroma_ui_request_redraw, NULL);

    AromaNode* tab1_container = aroma_container_create(win, 0, 120, w, h - 120);
    aroma_tabs_set_content(tabs, 0, &tab1_container, 1);
    snackbar =  aroma_snackbar_create((AromaNode*)tab1_container, "Hello, World!", 3000);
    if (snackbar) {
        aroma_snackbar_set_font((AromaNode*)snackbar, font);
        aroma_snackbar_set_action(snackbar, "Undo", on_undo, NULL);
    }
    AromaButton* btn = aroma_ui_create_button(tab1_container, "Click Me", w/2 - 150, h/2 + 100, 300, 100);
    AromaNode* dropdown = aroma_dropdown_create((AromaNode*)tab1_container, 300, 350, 250, 100);
    aroma_dropdown_setup_events(dropdown, aroma_ui_request_redraw, NULL);
    aroma_dropdown_add_option(dropdown, "Option 1");
    aroma_dropdown_add_option(dropdown, "Option 2");
    aroma_dropdown_set_font(dropdown, font);

    
    AromaNode* slider = aroma_slider_create((AromaNode*)tab1_container, w/2 - 200, h/2 + 500, 400, 50, 0, 100, 0);
    aroma_slider_setup_events(slider, aroma_ui_request_redraw, NULL);

    
    aroma_ui_on_button_click(btn, on_btn_click, NULL);
    aroma_button_set_font((AromaNode*)btn, font);
    snprintf(title, sizeof(title), "Window Size: %dx%d", w, h);

    AromaNode* label = aroma_ui_create_label((AromaNode*)tab1_container, title, 250, h/2, LABEL_STYLE_LABEL_LARGE);
    if(font) aroma_label_set_font(label, font);
    


    while(aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
       // aroma_node_invalidate_tree((AromaNode*)win);

        
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