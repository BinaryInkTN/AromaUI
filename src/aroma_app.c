#include "aroma_framework.h"
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

static int button_clicks = 0;
static AromaNode* button1 = NULL;
static AromaNode* button2 = NULL;
static AromaNode* toggle_switch = NULL;
static AromaNode* value_slider = NULL;
static AromaNode* input_textbox = NULL;
static int slider_value = 50;
static bool switch_state = false;
static char textbox_content[256] = "";
static bool needs_redraw = true;

static void mark_dirty(void* user_data)
{
    (void)user_data;
    needs_redraw = true;

    aroma_platform_request_window_update(0);  

}

bool on_button1_click(AromaNode* button_node, void* user_data)
{
    button_clicks++;
    printf("[BUTTON1] Clicked! Total clicks: %d\n", button_clicks);
    mark_dirty(NULL);
    return true;
}

bool on_button1_hover(AromaNode* button_node, void* user_data)
{
    printf("[BUTTON1] Hovered!\n");
    return true;
}

bool on_button2_click(AromaNode* button_node, void* user_data)
{
    printf("[BUTTON2] Reset clicked! Counter was: %d\n", button_clicks);
    button_clicks = 0;
    mark_dirty(NULL);
    return true;
}

bool on_switch_change(AromaNode* switch_node, void* user_data)
{
    switch_state = aroma_switch_get_state(switch_node);
    printf("[SWITCH] State changed to: %s\n", switch_state ? "ON" : "OFF");
    mark_dirty(NULL);
    return true;
}

bool on_slider_change(AromaNode* slider_node, void* user_data)
{
    slider_value = aroma_slider_get_value(slider_node);
    printf("[SLIDER] Value changed to: %d\n", slider_value);
    mark_dirty(NULL);
    return true;
}

bool on_textbox_text_changed(AromaNode* textbox_node, const char* text, void* user_data)
{
    strncpy(textbox_content, text, sizeof(textbox_content) - 1);
    textbox_content[sizeof(textbox_content) - 1] = '\0';
    printf("[TEXTBOX] Text changed to: %s\n", textbox_content);
    mark_dirty(NULL);
    return true;
}

bool on_textbox_focus_changed(AromaNode* textbox_node, bool focused, void* user_data)
{
    printf("[TEXTBOX] Focus changed to: %s\n", focused ? "focused" : "unfocused");
    mark_dirty(NULL);
    return true;
}

void window_update_callback(size_t window_id, void *data)
{
    if (!needs_redraw) {
        return;
    }

    aroma_graphics_clear(window_id, 0xF0F0F0); 

    aroma_graphics_fill_rectangle(
        window_id,
        0, 0, 800, 40,
        0x003366,
        false,
        0.0f
    );

    if (button1) {
        aroma_button_draw(button1, window_id);
    }
    if (button2) {
        aroma_button_draw(button2, window_id);
    }

    if (toggle_switch) {
        aroma_switch_draw(toggle_switch, window_id);
    }

    if (value_slider) {
        aroma_slider_draw(value_slider, window_id);
    }

    if (input_textbox) {
        aroma_textbox_draw(input_textbox, window_id);
    }

    char text_buffer[64];
    snprintf(text_buffer, sizeof(text_buffer), "Clicks: %d", button_clicks);
    AromaFont* default_font = (AromaFont*)data;
    if (default_font) {

        aroma_graphics_render_text(window_id, default_font, text_buffer, 110, 125, 0xFFFFFF);

        aroma_graphics_render_text(window_id, default_font, "Reset", 320, 125, 0xFFFFFF);

        aroma_graphics_render_text(window_id, default_font, "Toggle:", 50, 170, 0x333333);

        snprintf(text_buffer, sizeof(text_buffer), "Brightness: %d%%", slider_value);
        aroma_graphics_render_text(window_id, default_font, text_buffer, 50, 240, 0x333333);

        aroma_graphics_render_text(window_id, default_font, "Input text:", 50, 330, 0x333333);

        aroma_graphics_render_text(window_id, default_font, "Welcome to Aroma UI - Test new widgets", 50, 550, 0x333333);
    }

    aroma_graphics_swap_buffers(window_id);

    needs_redraw = false;
}

int main()
{
    printf("Initializing Aroma UI Framework...\n");
    aroma_memory_system_init();
    __node_system_init();
    aroma_event_system_init();

    aroma_platform_initialize();

    printf("Creating main window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma Scene Graph Example", 
        100,     

        100,     

        800,     

        600      

    );

    printf("Window created successfully (Scene node ID: %" PRIu64 ")\n", (uint64_t)main_window->node_id);

        aroma_event_set_root(main_window);

    printf("Loading system font...\n");
    AromaFont* default_font = aroma_font_create("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf", 18);
    if (!default_font) {
        printf("Warning: Failed to load font, text rendering will be disabled\n");
    } else {

        aroma_graphics_load_font_for_window(0, default_font);
        printf("Font loaded and glyphs prepared\n");
    }

    printf("Creating buttons...\n");
    button1 = aroma_button_create(main_window, "Click Me!", 100, 100, 150, 50);
    if (button1) {

        aroma_button_set_colors(button1, 0x0078D4, 0x005A9C, 0x004070, 0xFFFFFF);
        aroma_button_set_on_click(button1, on_button1_click, NULL);
        aroma_button_set_on_hover(button1, on_button1_hover, NULL);

        aroma_button_setup_events(button1, mark_dirty, NULL);
        printf("Button 1 created with ID: %" PRIu64 "\n", (uint64_t)button1->node_id);
    }

    button2 = aroma_button_create(main_window, "Reset", 300, 100, 150, 50);
    if (button2) {

        aroma_button_set_colors(button2, 0xFF6B35, 0xD85829, 0xA8320C, 0xFFFFFF);
        aroma_button_set_on_click(button2, on_button2_click, NULL);

        aroma_button_setup_events(button2, mark_dirty, NULL);
        printf("Button 2 created with ID: %" PRIu64 "\n", (uint64_t)button2->node_id);
    }

    printf("Creating switch widget...\n");
    toggle_switch = aroma_switch_create(main_window, 200, 160, 50, 30, false);
    if (toggle_switch) {
        aroma_switch_set_on_change(toggle_switch, on_switch_change, NULL);

        aroma_switch_setup_events(toggle_switch, mark_dirty, NULL);
        printf("Switch created with ID: %" PRIu64 "\n", (uint64_t)toggle_switch->node_id);
    }

    printf("Creating slider widget...\n");
    value_slider = aroma_slider_create(main_window, 50, 250, 300, 30, 0, 100, 50);
    if (value_slider) {
        aroma_slider_set_on_change(value_slider, on_slider_change, NULL);

        aroma_slider_setup_events(value_slider, mark_dirty, NULL);
        printf("Slider created with ID: %" PRIu64 "\n", (uint64_t)value_slider->node_id);
    }

    printf("Creating textbox widget...\n");
    input_textbox = aroma_textbox_create(main_window, 50, 350, 300, 40);
    if (input_textbox) {
        aroma_textbox_set_placeholder(input_textbox, "Enter text here...");
        aroma_textbox_set_font(input_textbox, default_font);
        aroma_textbox_set_on_text_changed(input_textbox, on_textbox_text_changed, NULL);
        aroma_textbox_set_on_focus_changed(input_textbox, on_textbox_focus_changed, NULL);

        aroma_textbox_setup_events(input_textbox, mark_dirty, on_textbox_text_changed, NULL);
        printf("Textbox created with ID: %" PRIu64 "\n", (uint64_t)input_textbox->node_id);
    }

    aroma_platform_set_window_update_callback(window_update_callback, default_font);

    __print_node_tree(main_window);

    printf("Running event loop...\n");
    printf("Click the buttons to test event handling\n");

    while (aroma_platform_run_event_loop())
    {

        aroma_event_process_queue();

        usleep(4000);
    }

    printf("Shutting down application...\n");
    if (button1) aroma_button_destroy(button1);
    if (button2) aroma_button_destroy(button2);
    if (toggle_switch) aroma_switch_destroy(toggle_switch);
    if (value_slider) aroma_slider_destroy(value_slider);
    if (input_textbox) aroma_textbox_destroy(input_textbox);
    if (default_font) aroma_font_destroy(default_font);
    aroma_event_system_shutdown();
    __node_system_destroy();

    printf("Application closed successfully.\n");
    return 0;
}

