#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char name[256];
    int age_index;
    int volume;
    bool notifications_enabled;
} FormData;

static FormData form = {
    .name = "",
    .age_index = -1,
    .volume = 50,
    .notifications_enabled = false
};

static AromaTextbox* txt_name = NULL;
static AromaDropdown* dd_age = NULL;
static AromaSlider* slider_volume = NULL;
static AromaSwitch* sw_notifications = NULL;
static AromaButton* btn_submit = NULL;
static AromaFont* font = NULL;

static bool on_name_changed(AromaTextbox* textbox, const char* text, void* user_data) {
    (void)textbox;
    (void)user_data;
    strncpy(form.name, text, sizeof(form.name) - 1);
    printf("Name: %s\n", form.name);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_age_changed(AromaDropdown* dd, const char* option, int index, void* user_data) {
    (void)dd;
    (void)user_data;
    form.age_index = index;
    printf("Age: %s\n", option);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_volume_changed(AromaSlider* slider, int value, void* user_data) {
    (void)slider;
    (void)user_data;
    form.volume = value;
    printf("Volume: %d%%\n", form.volume);
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_notifications_toggled(AromaSwitch* sw, bool state, void* user_data) {
    (void)user_data;
    (void)sw;
    form.notifications_enabled = state;
    printf("Notifications: %s\n", form.notifications_enabled ? "ON" : "OFF");
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool on_submit(AromaButton* button, void* user_data) {
    (void)button;
    (void)user_data;
    printf("Name: %s, Age: %d, Volume: %d%%, Notifications: %s\n",
           form.name, form.age_index, form.volume,
           form.notifications_enabled ? "ON" : "OFF");
    aroma_ui_request_redraw(NULL);
    return true;
}

void window_update_callback(size_t window_id, void* data) {
    (void)data;

    if (!aroma_ui_consume_redraw()) return;

    aroma_ui_begin_frame(window_id);
    aroma_ui_render_dirty_window(window_id, 0xF0F0F0);

    if (font) {
        aroma_graphics_render_text(window_id, font, "Name:", 50, 20, 0x333333);
        aroma_graphics_render_text(window_id, font, "Age:", 50, 100, 0x333333);
        aroma_graphics_render_text(window_id, font, "Volume:", 50, 180, 0x333333);
        aroma_graphics_render_text(window_id, font, "Notify:", 50, 260, 0x333333);

        char buf[64];
        snprintf(buf, sizeof(buf), "%d%%", form.volume);
        aroma_graphics_render_text(window_id, font, buf, 470, 180, 0x0078D7);
    }

    aroma_ui_render_dropdown_overlays(window_id);

    aroma_ui_end_frame(window_id);
    aroma_graphics_swap_buffers(window_id);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!aroma_ui_init()) {
        fprintf(stderr, "Failed to initialize Aroma UI\n");
        return 1;
    }

    font = aroma_ui_load_font("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf", 14);
    if (!font) {
        font = aroma_ui_load_font("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 14);
    }

    AromaWindow* window = aroma_ui_create_window("User Profile - Windows 7 Aero", 500, 600);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        aroma_ui_shutdown();
        return 1;
    }

    aroma_event_set_root((AromaNode*)window);

    if (font) aroma_ui_prepare_font_for_window(0, font);

    int y = 40;
    int dy = 80;

    txt_name = aroma_ui_create_textbox(window, "Enter your name...", 50, y, 400, 40);
    if (txt_name) {
        aroma_ui_on_textbox_change(txt_name, on_name_changed, NULL);
    }

    y += dy;
    dd_age = aroma_ui_create_dropdown(window, 50, y, 400, 40);
    if (dd_age) {
        const char* ages[] = {"18-25", "26-35", "36-45", "46-55", "56+"};
        for (int i = 0; i < 5; i++)
            aroma_ui_dropdown_add_option(dd_age, ages[i]);
        aroma_ui_on_dropdown_change(dd_age, on_age_changed, NULL);
    }
    aroma_dropdown_set_font((AromaNode*)dd_age, font);
    y += dy;
    slider_volume = aroma_ui_create_slider(window, 50, y, 400, 30);
    if (slider_volume) {
        aroma_ui_slider_set_range(slider_volume, 0, 100);
        aroma_ui_slider_set_value(slider_volume, 50);
        aroma_ui_on_slider_change(slider_volume, on_volume_changed, NULL);
    }

    y += dy;
    sw_notifications = aroma_ui_create_switch(window, "Enable Notifications", 50, y, 60, 30);
    if (sw_notifications) {
        aroma_ui_on_switch_change(sw_notifications, on_notifications_toggled, NULL);
    }

    y += dy + 40;
    btn_submit = aroma_ui_create_button(window, "Submit", 150, y, 200, 50);
    if (btn_submit) {
        aroma_ui_on_button_click(btn_submit, on_submit, NULL);
    }
    aroma_button_set_font((AromaNode*)btn_submit, font);

    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(4000);
    }

    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();

    return 0;
}

