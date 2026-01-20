#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

static AromaButton* g_btn1 = NULL;
static AromaTextbox* g_txt1 = NULL;
static AromaSlider* g_slider1 = NULL;
static AromaButton* g_btn2 = NULL;
static AromaSwitch* g_switch2 = NULL;
static AromaSlider* g_slider2 = NULL;

static bool on_window1_button_click(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    printf("Window 1: Button clicked!\n");
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_window1_slider_change(AromaSlider* slider, int value, void* user_data) {
    (void)slider;
    (void)user_data;
    printf("Window 1: Slider value = %d\n", value);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_window1_textbox_change(AromaTextbox* textbox, const char* text, void* user_data) {
    (void)textbox;
    (void)user_data;
    printf("Window 1: Text input = '%s'\n", text);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_window2_button_click(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    printf("Window 2: Button clicked!\n");
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_window2_switch_change(AromaSwitch* sw, bool state, void* user_data) {
    (void)sw;
    (void)user_data;
    printf("Window 2: Switch toggled = %s\n", state ? "ON" : "OFF");
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_window2_slider_change(AromaSlider* slider, int value, void* user_data) {
    (void)slider;
    (void)user_data;
    printf("Window 2: Volume = %d%%\n", value);
    aroma_ui_request_redraw(NULL);
    return true;
}

static void window_update_callback(size_t window_id, void* data) {
    (void)data;
    if (!aroma_ui_consume_redraw()) return;
    aroma_ui_render_dirty_window(window_id, 0xF0F0F0);
    aroma_ui_render_dropdown_overlays(window_id);
    aroma_graphics_swap_buffers(window_id);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!aroma_ui_init()) {
        printf("Failed to initialize Aroma UI\n");
        return 1;
    }

    AromaWindow* window1 = aroma_ui_create_window("Window 1: Input Controls", 700, 500);
    if (!window1) {
        printf("Failed to create window 1\n");
        aroma_ui_shutdown();
        return 1;
    }

    g_btn1 = aroma_ui_create_button(window1, "Submit Data", 250, 50, 200, 50);
    if (g_btn1) {
        aroma_ui_on_button_click(g_btn1, on_window1_button_click, NULL);
    }

    g_txt1 = aroma_ui_create_textbox(window1, "Enter name...", 50, 120, 600, 40);
    if (g_txt1) {
        aroma_ui_on_textbox_change(g_txt1, on_window1_textbox_change, NULL);
    }

    g_slider1 = aroma_ui_create_slider(window1, 50, 180, 600, 30);
    if (g_slider1) {
        aroma_ui_on_slider_change(g_slider1, on_window1_slider_change, NULL);
    }

    AromaWindow* window2 = aroma_ui_create_window("Window 2: Settings", 500, 400);
    if (!window2) {
        printf("Failed to create window 2\n");
        aroma_ui_destroy_window(window1);
        aroma_ui_shutdown();
        return 1;
    }

    g_btn2 = aroma_ui_create_button(window2, "Save Settings", 150, 50, 200, 50);
    if (g_btn2) {
        aroma_ui_on_button_click(g_btn2, on_window2_button_click, NULL);
    }

    g_switch2 = aroma_ui_create_switch(window2, "Enable Notifications", 50, 120, 200, 40);
    if (g_switch2) {
        aroma_ui_on_switch_change(g_switch2, on_window2_switch_change, NULL);
    }

    g_slider2 = aroma_ui_create_slider(window2, 50, 180, 400, 30);
    if (g_slider2) {
        aroma_ui_on_slider_change(g_slider2, on_window2_slider_change, NULL);
    }

    printf("Created %d windows with multiple widgets\n", aroma_ui_window_count());

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render_all();
        usleep(16000);  

    }

    aroma_ui_destroy_window(window1);
    aroma_ui_destroy_window(window2);
    aroma_ui_shutdown();

    printf("Goodbye!\n");
    return 0;
}

