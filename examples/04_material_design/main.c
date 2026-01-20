#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MD3_PRIMARY         0x6750A4
#define MD3_SURFACE         0xFFFBFE
#define MD3_ON_SURFACE      0x1C1B1F

static AromaFont* font = NULL;
static AromaButton* btn1 = NULL;
static AromaButton* btn2 = NULL;
static AromaButton* btn3 = NULL;
static AromaSwitch* toggle = NULL;
static AromaSlider* slider = NULL;
static AromaTextbox* textbox = NULL;
static AromaDropdown* dropdown = NULL;
static int count = 0;

static bool on_btn1_click(AromaButton* b, void* d) {
    count++; printf("Count: %d\n", count);
    aroma_ui_request_redraw(NULL); return true;
}

static bool on_btn2_click(AromaButton* b, void* d) {
    count = 0; printf("Reset!\n");
    aroma_ui_request_redraw(NULL); return true;
}

static bool on_btn3_click(AromaButton* b, void* d) {
    exit(0); return true;
}

void window_update(size_t wid, void* d) {
    if (!aroma_ui_consume_redraw()) return;
    aroma_ui_begin_frame(wid);
    aroma_ui_render_dirty_window(wid, MD3_SURFACE);

    if (font) {
        aroma_graphics_render_text(wid, font, "Material Design 3 Showcase", 30, 35, MD3_PRIMARY);
        aroma_graphics_render_text(wid, font, "Buttons (Filled)", 30, 75, MD3_ON_SURFACE);
        aroma_graphics_render_text(wid, font, "Add", 75, 113, 0xFFFFFF);
        aroma_graphics_render_text(wid, font, "Reset", 215, 113, 0xFFFFFF);
        aroma_graphics_render_text(wid, font, "Exit", 373, 113, 0xFFFFFF);

        aroma_graphics_render_text(wid, font, "Switch & Slider", 30, 175, MD3_ON_SURFACE);
        aroma_graphics_render_text(wid, font, "Text Input", 30, 275, MD3_ON_SURFACE);
        aroma_graphics_render_text(wid, font, "Dropdown Menu", 470, 275, MD3_ON_SURFACE);

        char buf[64];
        snprintf(buf, sizeof(buf), "Counter: %d", count);
        aroma_graphics_render_text(wid, font, buf, 30, 400, MD3_PRIMARY);
        aroma_graphics_render_text(wid, font, "Smooth antialiased edges • Material You colors", 30, 450, 0x79747E);
    }

    aroma_ui_end_frame(wid);
    aroma_graphics_swap_buffers(wid);
}

int main() {
    if (!aroma_ui_init()) return 1;
    
    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 14);
    if (!font) font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 14);
    
    AromaWindow* win = aroma_ui_create_window("Material Design 3", 800, 500);
    if (!win) { aroma_ui_shutdown(); return 1; }
    
    aroma_event_set_root((AromaNode*)win);
    if (font) aroma_ui_prepare_font_for_window(0, font);
    
    btn1 = aroma_ui_create_button(win, "Add", 30, 95, 120, 40);
    if (btn1) aroma_ui_on_button_click(btn1, on_btn1_click, NULL);
    
    btn2 = aroma_ui_create_button(win, "Reset", 170, 95, 120, 40);
    if (btn2) aroma_ui_on_button_click(btn2, on_btn2_click, NULL);
    
    btn3 = aroma_ui_create_button(win, "Exit", 310, 95, 120, 40);
    if (btn3) aroma_ui_on_button_click(btn3, on_btn3_click, NULL);
    
    toggle = aroma_ui_create_switch(win, "Enable", 30, 200, 60, 28);
    slider = aroma_ui_create_slider(win, 120, 205, 300, 20);
    textbox = aroma_ui_create_textbox(win, "Enter text...", 30, 300, 400, 45);
    if (textbox && font) aroma_textbox_set_font((AromaNode*)textbox, font);
    
    // Dropdown with Material Design 3 styling
    dropdown = aroma_ui_create_dropdown(win, 470, 300, 280, 45);
    if (dropdown) {
        aroma_dropdown_add_option((AromaNode*)dropdown, "Material Design 3");
        aroma_dropdown_add_option((AromaNode*)dropdown, "Smooth Antialiasing");
        aroma_dropdown_add_option((AromaNode*)dropdown, "Purple Theme");
        aroma_dropdown_add_option((AromaNode*)dropdown, "Rounded Corners");
        aroma_dropdown_add_option((AromaNode*)dropdown, "Elevation System");
        if (font) aroma_dropdown_set_font((AromaNode*)dropdown, font);
    }
    
    printf("Material Design 3 Example\n");
    printf("- Smooth antialiased shapes\n");
    printf("- MD3 color system (Purple theme)\n");
    printf("- All widgets: Button, Switch, Slider, Textbox, Dropdown\n");
    printf("- 20dp rounded corners on buttons\n\n");
    
    aroma_platform_set_window_update_callback(window_update, NULL);
    aroma_ui_request_redraw(NULL);
    
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
        usleep(4000);
    }
    
    aroma_ui_destroy_window(win);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    return 0;
}
