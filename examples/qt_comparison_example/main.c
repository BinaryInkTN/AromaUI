#include <aroma.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

typedef struct
{
    int x, y, w, h;
} Rect;

static inline int rect_center_x(Rect r) { return r.x + r.w / 2; }
static inline int rect_center_y(Rect r) { return r.y + r.h / 2; }

static int g_frame_count = 0;
static int g_total_frames = 0;
static double g_min_frame_ms = 1e9;
static double g_max_frame_ms = 0.0;
static clock_t g_run_start_time;
static clock_t g_second_start_time;

typedef struct
{
    int step;
    int click_count;
    int drag_progress;
    bool running;
    int frames_since_action;
    bool in_drag;
    int drag_x0, drag_y0, drag_x1, drag_y1;
    int drag_steps;
    int drag_current_step;
} TestState;

static TestState g_test_state = {0};
static AromaWindow *g_window = NULL;

static Rect g_btn_primary, g_btn_secondary, g_btn_cancel;
static Rect g_chk_a, g_chk_b, g_chk_c;
static Rect g_slider_volume, g_slider_brightness;
static Rect g_dropdown_rect;
static Rect g_switch1_rect, g_switch2_rect;
static Rect g_input_name, g_input_email, g_input_message;
static Rect g_list_rect, g_btn_save, g_btn_reset, g_btn_export;
static int g_list_row_height = 0;

static void fps_tick(void)
{
    clock_t now = clock();
    double frame_elapsed_s = (double)(now - g_second_start_time) / CLOCKS_PER_SEC;

    g_frame_count++;
    g_total_frames++;

    if (frame_elapsed_s > 0.0)
    {
        double frame_ms = frame_elapsed_s * 1000.0;
        if (g_frame_count > 1)
        {
            if (frame_ms < g_min_frame_ms)
                g_min_frame_ms = frame_ms;
            if (frame_ms > g_max_frame_ms)
                g_max_frame_ms = frame_ms;
        }
    }

    if (frame_elapsed_s >= 1.0)
    {
        printf("Aroma FPS: %.1f | Frame time: %.2fms | Total frames: %d\n",
               g_frame_count / frame_elapsed_s,
               (frame_elapsed_s * 1000.0) / g_frame_count,
               g_frame_count);
        g_frame_count = 0;
        g_second_start_time = now;
    }
}

static uint64_t dispatch_mouse_event_at(AromaWindow *window, AromaEventType type, int x, int y, const char *phase_label)
{
    AromaNode *hit = aroma_event_hit_test((AromaNode *)window, x, y);
    if (!hit)
    {
        printf("%s hit no node at (%d,%d)\n", phase_label, x, y);
        return 0;
    }

    printf("%s hit node ID: %" PRIu64 "\n", phase_label, hit->node_id);

    AromaEvent *ev = aroma_event_create_mouse(type, hit->node_id, x, y, 0);
    if (ev)
    {
        aroma_event_dispatch(ev);
        aroma_event_destroy(ev);
    }
    aroma_node_invalidate(hit);
    return hit->node_id;
}

static void do_click(AromaWindow *window, int x, int y)
{

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_ENTER, x, y, "MouseEnter");

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_CLICK, x, y, "MouseClick(down)");
    aroma_event_handle_pointer_move(x, y, true);

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_RELEASE, x, y, "MouseClick(up)");
    aroma_event_handle_pointer_move(x, y, false);

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_EXIT, x, y, "MouseExit");


}

static void start_drag(AromaWindow *window, int x0, int y0, int x1, int y1, int steps)
{
    g_test_state.in_drag = true;
    g_test_state.drag_x0 = x0;
    g_test_state.drag_y0 = y0;
    g_test_state.drag_x1 = x1;
    g_test_state.drag_y1 = y1;
    g_test_state.drag_steps = steps;
    g_test_state.drag_current_step = 0;

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_ENTER, x0, y0, "DragEnter");
    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_CLICK, x0, y0, "DragStart(down)");
    aroma_event_handle_pointer_move(x0, y0, true);
}

static bool continue_drag(AromaWindow *window)
{
    if (!g_test_state.in_drag)
        return false;

    g_test_state.drag_current_step++;

    if (g_test_state.drag_current_step <= g_test_state.drag_steps)
    {
        double t = (double)g_test_state.drag_current_step / (double)g_test_state.drag_steps;
        int x = g_test_state.drag_x0 + (int)((g_test_state.drag_x1 - g_test_state.drag_x0) * t);
        int y = g_test_state.drag_y0 + (int)((g_test_state.drag_y1 - g_test_state.drag_y0) * t);

        dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_MOVE, x, y, "DragMove");
        aroma_event_handle_pointer_move(x, y, true);
        return true;
    }
    else
    {
        dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_RELEASE, g_test_state.drag_x1, g_test_state.drag_y1, "DragEnd(up)");
        aroma_event_handle_pointer_move(g_test_state.drag_x1, g_test_state.drag_y1, false);
        dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_EXIT, g_test_state.drag_x1, g_test_state.drag_y1, "DragExit");
        g_test_state.in_drag = false;
        return false;
    }
}

static void run_test_step(AromaWindow *window)
{
    if (!g_test_state.running)
        return;

    if (g_test_state.in_drag)
    {
        bool still_dragging = continue_drag(window);
        if (!still_dragging)
        {
            g_test_state.step++;
            g_test_state.click_count = 0;
            g_test_state.frames_since_action = 0;
        }
        return;
    }

    g_test_state.frames_since_action++;
    if (g_test_state.frames_since_action < 3)
    {
        return;
    }
    g_test_state.frames_since_action = 0;

    switch (g_test_state.step)
    {
    case 0:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 1: Clicking top buttons ---\n");
            do_click(window, rect_center_x(g_btn_primary), rect_center_y(g_btn_primary));
        }
        else if (g_test_state.click_count == 1)
        {
            do_click(window, rect_center_x(g_btn_secondary), rect_center_y(g_btn_secondary));
        }
        else if (g_test_state.click_count == 2)
        {
            do_click(window, rect_center_x(g_btn_cancel), rect_center_y(g_btn_cancel));
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        g_test_state.click_count++;
        break;

    case 1:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 2: Toggling checkboxes A/B/C ---\n");
            do_click(window, rect_center_x(g_chk_a), rect_center_y(g_chk_a));
        }
        else if (g_test_state.click_count == 1)
        {
            do_click(window, rect_center_x(g_chk_b), rect_center_y(g_chk_b));
        }
        else if (g_test_state.click_count == 2)
        {
            do_click(window, rect_center_x(g_chk_c), rect_center_y(g_chk_c));
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        g_test_state.click_count++;
        break;

    case 2:
        printf("\n--- Step 3: Dragging Volume slider left -> right ---\n");
        start_drag(window,
                   g_slider_volume.x + 5, g_slider_volume.y + g_slider_volume.h / 2,
                   g_slider_volume.x + g_slider_volume.w - 5, g_slider_volume.y + g_slider_volume.h / 2,
                   10);
        break;

    case 3:
        printf("\n--- Step 4: Dragging Brightness slider right -> left ---\n");
        start_drag(window,
                   g_slider_brightness.x + g_slider_brightness.w - 5, g_slider_brightness.y + g_slider_brightness.h / 2,
                   g_slider_brightness.x + 5, g_slider_brightness.y + g_slider_brightness.h / 2,
                   10);
        break;

    case 4:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 5: Opening dropdown and clicking through options ---\n");
            do_click(window, rect_center_x(g_dropdown_rect), rect_center_y(g_dropdown_rect));
        }
        else if (g_test_state.click_count <= 5)
        {
            int opt_y = g_dropdown_rect.y + g_dropdown_rect.h +
                        (g_test_state.click_count - 1) * 30 + 15;
            do_click(window, rect_center_x(g_dropdown_rect), opt_y);
        }
        g_test_state.click_count++;
        if (g_test_state.click_count > 5)
        {
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        break;

    case 5:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 6: Flipping WiFi and Bluetooth switches ---\n");
            do_click(window, rect_center_x(g_switch1_rect), rect_center_y(g_switch1_rect));
        }
        else if (g_test_state.click_count == 1)
        {
            do_click(window, rect_center_x(g_switch2_rect), rect_center_y(g_switch2_rect));
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        g_test_state.click_count++;
        break;

    case 6:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 7: Focusing text fields (name, email, message) ---\n");
            do_click(window, rect_center_x(g_input_name), rect_center_y(g_input_name));
        }
        else if (g_test_state.click_count == 1)
        {
            do_click(window, rect_center_x(g_input_email), rect_center_y(g_input_email));
        }
        else if (g_test_state.click_count == 2)
        {
            do_click(window, rect_center_x(g_input_message), rect_center_y(g_input_message));
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        g_test_state.click_count++;
        break;

    case 7:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 8: Clicking every row in the list view ---\n");
        }
        if (g_test_state.click_count < 6)
        {
            int row_y = g_list_rect.y + g_test_state.click_count * g_list_row_height + g_list_row_height / 2;
            do_click(window, g_list_rect.x + g_list_rect.w / 2, row_y);
            g_test_state.click_count++;
        }
        else
        {
            g_test_state.step++;
            g_test_state.click_count = 0;
        }
        break;

    case 8:
        if (g_test_state.click_count == 0)
        {
            printf("\n--- Step 9: Clicking Save / Reset / Export ---\n");
            do_click(window, rect_center_x(g_btn_save), rect_center_y(g_btn_save));
        }
        else if (g_test_state.click_count == 1)
        {
            do_click(window, rect_center_x(g_btn_reset), rect_center_y(g_btn_reset));
        }
        else if (g_test_state.click_count == 2)
        {
            do_click(window, rect_center_x(g_btn_export), rect_center_y(g_btn_export));
            g_test_state.running = false;

            double total_elapsed_s = (double)(clock() - g_run_start_time) / CLOCKS_PER_SEC;
            printf("\n=== Aroma UI Scripted Stress Test Complete ===\n");
            printf("Total frames:      %d\n", g_total_frames);
            printf("Total wall time:   %.3fs\n", total_elapsed_s);
            printf("Average FPS:       %.1f\n", total_elapsed_s > 0.0 ? g_total_frames / total_elapsed_s : 0.0);
            if (g_max_frame_ms > 0.0 && g_max_frame_ms < 1e9)
            {
                printf("Min frame time:    %.2fms\n", g_min_frame_ms);
                printf("Max frame time:    %.2fms\n", g_max_frame_ms);
            }
            return;
        }
        g_test_state.click_count++;
        break;

    default:
        g_test_state.running = false;
        break;
    }
}

bool on_button_click(AromaNode *node, void *user_data)
{
    printf("Button: %s\n", (char *)user_data);
    return true;
}

void on_checkbox_changed(bool checked, void *user_data)
{
    printf("Checkbox %s: %s\n", (char *)user_data, checked ? "checked" : "unchecked");
}

void on_dropdown_changed(int index, const char *value, void *user_data)
{
    printf("Dropdown: %s\n", value);
}

bool on_slider_change(AromaNode *node, void *user_data)
{
    printf("Slider: %d\n", aroma_slider_get_value(node));
    return true;
}

void on_list_item_click(int index, void *user_data)
{
    printf("List item: %d\n", index);
}

int main(void)
{

    set_minimum_log_level(1000);
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);

    g_window = aroma_ui_create_window("Aroma UI - Scripted Stress Test", 800, 600);
    AromaWindow *window = g_window;

    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);
    AromaFont *header_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);

    aroma_ui_label((AromaNode *)window, "Performance Test Dashboard", 20, 15, LABEL_STYLE_LABEL_LARGE, header_font);

    g_btn_primary = (Rect){20, 55, 140, 35};
    g_btn_secondary = (Rect){180, 55, 120, 35};
    g_btn_cancel = (Rect){320, 55, 100, 35};
    aroma_ui_button((AromaNode *)window, "Primary Action", g_btn_primary.x, g_btn_primary.y, g_btn_primary.w, g_btn_primary.h, on_button_click, "primary", text_font);
    aroma_ui_button((AromaNode *)window, "Secondary", g_btn_secondary.x, g_btn_secondary.y, g_btn_secondary.w, g_btn_secondary.h, on_button_click, "secondary", text_font);
    aroma_ui_button((AromaNode *)window, "Cancel", g_btn_cancel.x, g_btn_cancel.y, g_btn_cancel.w, g_btn_cancel.h, on_button_click, "cancel", text_font);

    aroma_ui_label((AromaNode *)window, "CPU Usage", 20, 110, LABEL_STYLE_LABEL_SMALL, text_font);
    aroma_ui_progressbar((AromaNode *)window, 20, 135, 200, 15, PROGRESS_TYPE_DETERMINATE, 0.45f);

    aroma_ui_label((AromaNode *)window, "Memory Usage", 240, 110, LABEL_STYLE_LABEL_SMALL, text_font);
    aroma_ui_progressbar((AromaNode *)window, 240, 135, 200, 15, PROGRESS_TYPE_DETERMINATE, 0.72f);

    g_chk_a = (Rect){20, 170, 200, 25};
    g_chk_b = (Rect){20, 200, 200, 25};
    g_chk_c = (Rect){20, 230, 200, 25};
    aroma_ui_checkbox((AromaNode *)window, "Enable feature A", g_chk_a.x, g_chk_a.y, g_chk_a.w, g_chk_a.h, on_checkbox_changed, "feature_a", text_font);
    aroma_ui_checkbox((AromaNode *)window, "Enable feature B", g_chk_b.x, g_chk_b.y, g_chk_b.w, g_chk_b.h, on_checkbox_changed, "feature_b", text_font);
    aroma_ui_checkbox((AromaNode *)window, "Enable feature C", g_chk_c.x, g_chk_c.y, g_chk_c.w, g_chk_c.h, on_checkbox_changed, "feature_c", text_font);

    aroma_ui_label((AromaNode *)window, "Volume", 250, 170, LABEL_STYLE_LABEL_SMALL, text_font);
    g_slider_volume = (Rect){250, 195, 200, 25};
    aroma_ui_slider((AromaNode *)window, g_slider_volume.x, g_slider_volume.y, g_slider_volume.w, g_slider_volume.h, 0, 100, 75, on_slider_change, NULL);

    aroma_ui_label((AromaNode *)window, "Brightness", 250, 225, LABEL_STYLE_LABEL_SMALL, text_font);
    g_slider_brightness = (Rect){250, 250, 200, 25};
    aroma_ui_slider((AromaNode *)window, g_slider_brightness.x, g_slider_brightness.y, g_slider_brightness.w, g_slider_brightness.h, 0, 100, 60, on_slider_change, NULL);

    aroma_ui_label((AromaNode *)window, "Select Option", 470, 110, LABEL_STYLE_LABEL_SMALL, text_font);
    char *options[] = {"Option 1", "Option 2", "Option 3", "Option 4", "Option 5"};
    g_dropdown_rect = (Rect){470, 135, 200, 30};
    aroma_ui_dropdown((AromaNode *)window, g_dropdown_rect.x, g_dropdown_rect.y, g_dropdown_rect.w, g_dropdown_rect.h, options, 5, on_dropdown_changed, NULL, text_font);

    aroma_ui_label((AromaNode *)window, "Toggle Switches", 470, 180, LABEL_STYLE_LABEL_SMALL, text_font);
    g_switch1_rect = (Rect){470, 205, 50, 25};
    aroma_ui_switch((AromaNode *)window, g_switch1_rect.x, g_switch1_rect.y, g_switch1_rect.w, g_switch1_rect.h, true, NULL, "switch1");
    aroma_ui_label((AromaNode *)window, "WiFi", 530, 207, LABEL_STYLE_LABEL_SMALL, text_font);

    g_switch2_rect = (Rect){470, 240, 50, 25};
    aroma_ui_switch((AromaNode *)window, g_switch2_rect.x, g_switch2_rect.y, g_switch2_rect.w, g_switch2_rect.h, false, NULL, "switch2");
    aroma_ui_label((AromaNode *)window, "Bluetooth", 530, 242, LABEL_STYLE_LABEL_SMALL, text_font);

    aroma_ui_divider((AromaNode *)window, 20, 285, 760, DIVIDER_ORIENTATION_HORIZONTAL);

    aroma_ui_label((AromaNode *)window, "Text Input Fields", 20, 295, LABEL_STYLE_LABEL_LARGE, header_font);

    g_input_name = (Rect){20, 330, 350, 35};
    g_input_email = (Rect){20, 375, 350, 35};
    g_input_message = (Rect){20, 420, 350, 80};
    aroma_ui_textbox((AromaNode *)window, g_input_name.x, g_input_name.y, g_input_name.w, g_input_name.h, "Enter name...", NULL, NULL, text_font);
    aroma_ui_textbox((AromaNode *)window, g_input_email.x, g_input_email.y, g_input_email.w, g_input_email.h, "Enter email...", NULL, NULL, text_font);
    aroma_ui_textbox((AromaNode *)window, g_input_message.x, g_input_message.y, g_input_message.w, g_input_message.h, "Enter message...", NULL, NULL, text_font);

    aroma_ui_label((AromaNode *)window, "Items List", 400, 295, LABEL_STYLE_LABEL_LARGE, header_font);
    g_list_rect = (Rect){400, 330, 380, 170};
    AromaNode *list = aroma_ui_listview((AromaNode *)window, g_list_rect.x, g_list_rect.y, g_list_rect.w, g_list_rect.h, on_list_item_click, NULL, text_font);
    aroma_listview_add_item(list, "Item 1 - Dashboard", "", NULL);
    aroma_listview_add_item(list, "Item 2 - Reports", "", NULL);
    aroma_listview_add_item(list, "Item 3 - Analytics", "", NULL);
    aroma_listview_add_item(list, "Item 4 - Settings", "", NULL);
    aroma_listview_add_item(list, "Item 5 - Help", "", NULL);
    aroma_listview_add_item(list, "Item 6 - About", "", NULL);

    g_list_row_height = g_list_rect.h / 6;

    g_btn_save = (Rect){20, 520, 100, 35};
    g_btn_reset = (Rect){140, 520, 100, 35};
    g_btn_export = (Rect){260, 520, 120, 35};
    aroma_ui_button((AromaNode *)window, "Save", g_btn_save.x, g_btn_save.y, g_btn_save.w, g_btn_save.h, on_button_click, "save", text_font);
    aroma_ui_button((AromaNode *)window, "Reset", g_btn_reset.x, g_btn_reset.y, g_btn_reset.w, g_btn_reset.h, on_button_click, "reset", text_font);
    aroma_ui_button((AromaNode *)window, "Export Data", g_btn_export.x, g_btn_export.y, g_btn_export.w, g_btn_export.h, on_button_click, "export", text_font);

    aroma_event_set_root((AromaNode *)window);

    g_run_start_time = clock();
    g_second_start_time = g_run_start_time;

    printf("=== Aroma UI Scripted Stress Test Started ===\n");

    g_test_state.running = true;
    g_test_state.step = 0;
    g_test_state.click_count = 0;
    g_test_state.frames_since_action = 0;
    g_test_state.in_drag = false;
    while (g_test_state.running)
    {
                run_test_step(window);

        aroma_ui_process_events();
       
        aroma_ui_render(window);
        fps_tick();    
        


#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    aroma_font_destroy(text_font);
    aroma_font_destroy(header_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}