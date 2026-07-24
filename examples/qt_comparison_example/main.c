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

/* ---------------------------------------------------------------------
 * Shared benchmark contract (must match the Qt harness exactly):
 *   - fixed total wall-clock script duration, split evenly across 9 steps
 *   - fixed settle delay before the script starts
 *   - repaint tick and FPS sampling run on their own independent cadence
 *   - a trailing partial FPS window is reported, never silently dropped
 *   - the window is invalidated only when a driven action actually
 *     happened, never unconditionally every loop iteration
 * --------------------------------------------------------------------- */
static const double SETTLE_DELAY_MS = 200.0;
static const int STEP_COUNT = 9;
static const double TOTAL_SCRIPT_DURATION_MS = 6000.0;
static const double PER_STEP_BUDGET_MS = TOTAL_SCRIPT_DURATION_MS / STEP_COUNT;
static const int REPAINT_TICK_MS = 16;
static const double FPS_WINDOW_MS = 1000.0;

static int g_total_frames = 0;
static double g_min_frame_ms = 1e9;
static double g_max_frame_ms = 0.0;
static double g_run_start_time;

/* Windowed FPS sampling, independent of the test-driving cadence below,
 * mirroring the Qt harness's sampleFpsWindow(). */
static int g_fps_window_frames = 0;
static double g_fps_window_start = 0.0;
static double *g_fps_samples = NULL;
static int g_fps_samples_count = 0;
static int g_fps_samples_cap = 0;

static void fps_samples_push(double fps)
{
    if (g_fps_samples_count >= g_fps_samples_cap)
    {
        g_fps_samples_cap = g_fps_samples_cap ? g_fps_samples_cap * 2 : 16;
        g_fps_samples = (double *)realloc(g_fps_samples, sizeof(double) * g_fps_samples_cap);
    }
    g_fps_samples[g_fps_samples_count++] = fps;
}

/* Wall-clock time source. clock() measures CPU time consumed by the
 * process, not wall time -- and these loops spend most of each iteration
 * inside usleep(), which yields the CPU without necessarily accumulating
 * CPU time. That mismatch means a clock()-based "wait 200ms" can take far
 * longer than 200ms of real time to satisfy, which is why the settle loop
 * (and every sub-action / drag deadline below it) appeared to hang.
 * CLOCK_MONOTONIC tracks actual elapsed time and isn't affected by
 * wall-clock adjustments (NTP, DST, manual changes), which is what every
 * deadline in this file actually wants. */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Elapsed milliseconds between two now_ms() samples. */
static double ms_between(double start, double end)
{
    return end - start;
}

/* Flushes the current FPS window either when it reaches the shared
 * FPS_WINDOW_MS boundary, or when force_flush is set at shutdown so a
 * trailing partial window is still reported instead of discarded. */
static void sample_fps_window(bool force_flush)
{
    double now = now_ms();
    double elapsed_ms = ms_between(g_fps_window_start, now);

    if (elapsed_ms >= FPS_WINDOW_MS || (force_flush && g_fps_window_frames > 0 && elapsed_ms > 0))
    {
        double elapsed_s = elapsed_ms / 1000.0;
        double fps = g_fps_window_frames / elapsed_s;
        printf("Aroma FPS: %.1f | Frame time: %.2fms | Total frames: %d%s\n",
               fps,
               elapsed_ms / g_fps_window_frames,
               g_fps_window_frames,
               force_flush ? " (final partial window)" : "");
        fps_samples_push(fps);
        g_fps_window_frames = 0;
        g_fps_window_start = now;
    }
}

typedef struct
{
    int step;
    int sub_index;
    int sub_count;
    bool running;
    double step_deadline;    /* wall-clock deadline (ms, monotonic) for the current sub-action */
    bool in_drag;
    int drag_x0, drag_y0, drag_x1, drag_y1;
    int drag_steps;
    int drag_current_step;
    double drag_step_deadline;
    double drag_step_ms;
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

/* Marks the window dirty only when a driven action actually happened,
 * instead of invalidating unconditionally on every loop iteration. */
static void mark_dirty_from_action(AromaWindow *window)
{
    aroma_node_invalidate((AromaNode *)window);
}

/* Periodic repaint tick, decoupled from the test-driving cadence, so FPS
 * reflects render cost rather than however fast the script happens to run.
 * This is the Aroma equivalent of Qt's repaintTimer firing every 16ms. */
static double g_last_repaint_tick = 0.0;
static void maybe_periodic_repaint_tick(AromaWindow *window)
{
    double now = now_ms();
    if (g_last_repaint_tick == 0.0 || ms_between(g_last_repaint_tick, now) >= REPAINT_TICK_MS)
    {
        aroma_node_invalidate((AromaNode *)window);
        g_last_repaint_tick = now;
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
    mark_dirty_from_action(window);
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

/* Begins a drag. step_budget_ms is this step's total wall-clock allowance
 * (PER_STEP_BUDGET_MS), subdivided evenly across `steps` substeps -- so drag
 * smoothness scales with whatever budget the step owns, rather than a
 * hardcoded per-substep constant that only matched the Qt side by luck. */
static void start_drag(AromaWindow *window, int x0, int y0, int x1, int y1, int steps, double step_budget_ms)
{
    g_test_state.in_drag = true;
    g_test_state.drag_x0 = x0;
    g_test_state.drag_y0 = y0;
    g_test_state.drag_x1 = x1;
    g_test_state.drag_y1 = y1;
    g_test_state.drag_steps = steps;
    g_test_state.drag_current_step = 0;
    /* Reserve 15% of the step budget for the release settle, same split the
     * Qt harness uses. */
    g_test_state.drag_step_ms = (step_budget_ms * 0.85) / steps;

    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_ENTER, x0, y0, "DragEnter");
    dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_CLICK, x0, y0, "DragStart(down)");
    aroma_event_handle_pointer_move(x0, y0, true);

    g_test_state.drag_step_deadline = now_ms() + g_test_state.drag_step_ms;
}

/* Advances the drag by wall-clock deadline rather than "one substep per loop
 * iteration" -- so drag pacing is real time, matching QTest::qWait, and
 * doesn't silently speed up or slow down if the frame rate changes. */
static bool continue_drag(AromaWindow *window)
{
    if (!g_test_state.in_drag)
        return false;

    double now = now_ms();
    if (now < g_test_state.drag_step_deadline)
        return true; /* still waiting out this substep's time slice */

    g_test_state.drag_current_step++;

    if (g_test_state.drag_current_step <= g_test_state.drag_steps)
    {
        double t = (double)g_test_state.drag_current_step / (double)g_test_state.drag_steps;
        int x = g_test_state.drag_x0 + (int)((g_test_state.drag_x1 - g_test_state.drag_x0) * t);
        int y = g_test_state.drag_y0 + (int)((g_test_state.drag_y1 - g_test_state.drag_y0) * t);

        dispatch_mouse_event_at(window, EVENT_TYPE_MOUSE_MOVE, x, y, "DragMove");
        aroma_event_handle_pointer_move(x, y, true);

        g_test_state.drag_step_deadline = now + g_test_state.drag_step_ms;
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

/* Starts a fresh sub-action's wall-clock wait window: this step's total
 * budget (PER_STEP_BUDGET_MS) divided evenly across sub_count sub-actions,
 * mirroring the Qt harness's per-substep waitAndSample() slices. */
static void begin_sub_action(int sub_count)
{
    g_test_state.sub_count = sub_count;
    g_test_state.sub_index = 0;
    double sub_ms = PER_STEP_BUDGET_MS / (double)sub_count;
    g_test_state.step_deadline = now_ms() + sub_ms;
}

static bool sub_action_deadline_reached(void)
{
    return now_ms() >= g_test_state.step_deadline;
}

static void advance_sub_action(void)
{
    g_test_state.sub_index++;
    double sub_ms = PER_STEP_BUDGET_MS / (double)g_test_state.sub_count;
    g_test_state.step_deadline = now_ms() + sub_ms;
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
            printf("DEBUG: Drag complete, advancing to step %d\n", g_test_state.step + 1);
            g_test_state.step++;
            g_test_state.sub_index = 0;
        }
        return;
    }

    /* Wall-clock gate: wait out the current sub-action's deadline before
     * firing the next one, matching QTest::qWait semantics instead of
     * gating on a fixed number of frames. */
    if (g_test_state.sub_count > 0 && !sub_action_deadline_reached())
        return;

    printf("DEBUG: step=%d sub_index=%d sub_count=%d\n", 
           g_test_state.step, g_test_state.sub_index, g_test_state.sub_count);
    fflush(stdout);

    switch (g_test_state.step)
    {
    case 0:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 1: Clicking top buttons ---\n");
            begin_sub_action(3);
            do_click(window, rect_center_x(g_btn_secondary), rect_center_y(g_btn_secondary));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 1)
        {
            do_click(window, rect_center_x(g_btn_cancel), rect_center_y(g_btn_cancel));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 2)
        {
            do_click(window, rect_center_x(g_btn_primary), rect_center_y(g_btn_primary));
            printf("DEBUG: Step 1 complete, advancing to step 2\n");
            g_test_state.step++;
            g_test_state.sub_index = 0;
            g_test_state.sub_count = 0;
        }
        break;

    case 1:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 2: Toggling checkboxes A/B/C ---\n");
            begin_sub_action(3);
            do_click(window, rect_center_x(g_chk_a), rect_center_y(g_chk_a));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 1)
        {
            do_click(window, rect_center_x(g_chk_b), rect_center_y(g_chk_b));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 2)
        {
            do_click(window, rect_center_x(g_chk_c), rect_center_y(g_chk_c));
            printf("DEBUG: Step 2 complete, advancing to step 3\n");
            g_test_state.step++;
            g_test_state.sub_index = 0;
            g_test_state.sub_count = 0;
        }
        break;

    case 2:
        printf("\n--- Step 3: Dragging Volume slider left -> right ---\n");
        printf("DEBUG: Starting volume drag\n");
        start_drag(window,
                   g_slider_volume.x + 5, g_slider_volume.y + g_slider_volume.h / 2,
                   g_slider_volume.x + g_slider_volume.w - 5, g_slider_volume.y + g_slider_volume.h / 2,
                   10, PER_STEP_BUDGET_MS);
        break;

    case 3:
        printf("\n--- Step 4: Dragging Brightness slider right -> left ---\n");
        printf("DEBUG: Starting brightness drag\n");
        start_drag(window,
                   g_slider_brightness.x + g_slider_brightness.w - 5, g_slider_brightness.y + g_slider_brightness.h / 2,
                   g_slider_brightness.x + 5, g_slider_brightness.y + g_slider_brightness.h / 2,
                   10, PER_STEP_BUDGET_MS);
        break;

    case 4:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 5: Opening dropdown and clicking through options ---\n");
            printf("DEBUG: Starting dropdown interaction\n");
            /* 11 sub-actions: 1 initial open + 5 cycles of (reopen + click) */
            begin_sub_action(11);
            do_click(window, rect_center_x(g_dropdown_rect), rect_center_y(g_dropdown_rect));
            advance_sub_action();
        }
        else if (g_test_state.sub_index >= 1 && g_test_state.sub_index <= 10)
        {
            /* Odd sub_index (1,3,5,7,9): reopen dropdown
             * Even sub_index (2,4,6,8,10): click option */
            if (g_test_state.sub_index % 2 == 1)
            {
                /* Reopen dropdown */
                printf("DEBUG: Reopening dropdown\n");
                do_click(window, rect_center_x(g_dropdown_rect), rect_center_y(g_dropdown_rect));
                advance_sub_action();
            }
            else
            {
                /* Click the option */
                int option_idx = (g_test_state.sub_index - 2) / 2; /* 0,1,2,3,4 */
                int opt_y = g_dropdown_rect.y + g_dropdown_rect.h + option_idx * 30 + 15;
                printf("DEBUG: Clicking dropdown option %d at y=%d\n", option_idx + 1, opt_y);
                do_click(window, rect_center_x(g_dropdown_rect), opt_y);
                
                if (g_test_state.sub_index == 10)
                {
                    printf("DEBUG: Step 5 complete, advancing to step 6\n");
                    g_test_state.step++;
                    g_test_state.sub_index = 0;
                    g_test_state.sub_count = 0;
                }
                else
                {
                    advance_sub_action();
                }
            }
        }
        break;

    case 5:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 6: Flipping WiFi and Bluetooth switches ---\n");
            begin_sub_action(2);
            do_click(window, rect_center_x(g_switch1_rect), rect_center_y(g_switch1_rect));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 1)
        {
            do_click(window, rect_center_x(g_switch2_rect), rect_center_y(g_switch2_rect));
            printf("DEBUG: Step 6 complete, advancing to step 7\n");
            g_test_state.step++;
            g_test_state.sub_index = 0;
            g_test_state.sub_count = 0;
        }
        break;

    case 6:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 7: Focusing text fields (name, email, message) ---\n");
            begin_sub_action(3);
            do_click(window, rect_center_x(g_input_name), rect_center_y(g_input_name));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 1)
        {
            do_click(window, rect_center_x(g_input_email), rect_center_y(g_input_email));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 2)
        {
            do_click(window, rect_center_x(g_input_message), rect_center_y(g_input_message));
            printf("DEBUG: Step 7 complete, advancing to step 8\n");
            g_test_state.step++;
            g_test_state.sub_index = 0;
            g_test_state.sub_count = 0;
        }
        break;

    case 7:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 8: Clicking every row in the list view ---\n");
            begin_sub_action(6);
        }
        if (g_test_state.sub_index < 6)
        {
            int row_y = g_list_rect.y + g_test_state.sub_index * g_list_row_height + g_list_row_height / 2;
            printf("DEBUG: Clicking list row %d at y=%d\n", g_test_state.sub_index, row_y);
            do_click(window, g_list_rect.x + g_list_rect.w / 2, row_y);
            if (g_test_state.sub_index == 5)
            {
                printf("DEBUG: Step 8 complete, advancing to step 9\n");
                g_test_state.step++;
                g_test_state.sub_index = 0;
                g_test_state.sub_count = 0;
            }
            else
            {
                advance_sub_action();
            }
        }
        break;

    case 8:
        if (g_test_state.sub_index == 0)
        {
            printf("\n--- Step 9: Clicking Save / Reset / Export ---\n");
            begin_sub_action(3);
            do_click(window, rect_center_x(g_btn_save), rect_center_y(g_btn_save));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 1)
        {
            do_click(window, rect_center_x(g_btn_reset), rect_center_y(g_btn_reset));
            advance_sub_action();
        }
        else if (g_test_state.sub_index == 2)
        {
            do_click(window, rect_center_x(g_btn_export), rect_center_y(g_btn_export));
            printf("DEBUG: Step 9 complete, test finished\n");
            g_test_state.running = false;
            return;
        }
        break;

    default:
        printf("DEBUG: Unknown step %d, stopping test\n", g_test_state.step);
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

    /* Fixed settle delay before the script starts, matching the Qt harness. */
    printf("DEBUG: entering settle loop\n");
    fflush(stdout);
    double settle_start = now_ms();
    int settle_iterations = 0;
    while (ms_between(settle_start, now_ms()) < SETTLE_DELAY_MS)
    {
        settle_iterations++;
        if (settle_iterations % 50 == 0)
        {
            printf("DEBUG: settle loop iter=%d elapsed_ms=%.2f\n",
                   settle_iterations, ms_between(settle_start, now_ms()));
            fflush(stdout);
        }
        aroma_ui_process_events();
        maybe_periodic_repaint_tick(window);
        aroma_ui_render(window);
        usleep(REPAINT_TICK_MS * 1000);
    }
    printf("DEBUG: exited settle loop after %d iterations\n", settle_iterations);
    fflush(stdout);

    g_run_start_time = now_ms();
    g_fps_window_start = g_run_start_time;
    g_last_repaint_tick = 0.0;

    printf("=== Aroma UI Scripted Stress Test Started ===\n");
    fflush(stdout);

    g_test_state.running = true;
    g_test_state.step = 0;
    g_test_state.sub_index = 0;
    g_test_state.sub_count = 0;
    g_test_state.in_drag = false;
    while (g_test_state.running)
    {
        run_test_step(window);
        
        /* Exit immediately when the test finishes */
        if (!g_test_state.running)
            break;

        aroma_ui_process_events();
        maybe_periodic_repaint_tick(window);
        aroma_ui_render(window);

        double before_frame = now_ms();
        g_total_frames++;
        g_fps_window_frames++;
        double frame_ms = ms_between(g_last_repaint_tick ? g_last_repaint_tick : g_run_start_time, before_frame);
        if (frame_ms > 0 && g_total_frames > 1)
        {
            if (frame_ms < g_min_frame_ms) g_min_frame_ms = frame_ms;
            if (frame_ms > g_max_frame_ms) g_max_frame_ms = frame_ms;
        }
        sample_fps_window(false);

#ifdef __EMSCRIPTEN__
        emscripten_sleep(REPAINT_TICK_MS);
#else
        usleep(REPAINT_TICK_MS * 1000);
#endif
    }

    /* Flush any remaining FPS window data and print final summary */
    sample_fps_window(true);

    double total_elapsed_s = ms_between(g_run_start_time, now_ms()) / 1000.0;
    printf("\n=== Aroma UI Scripted Stress Test Complete ===\n");
    printf("Total frames:      %d\n", g_total_frames);
    printf("Total wall time:   %.3fs\n", total_elapsed_s);
    printf("Average FPS:       %.1f\n", total_elapsed_s > 0.0 ? g_total_frames / total_elapsed_s : 0.0);
    if (g_fps_samples_count > 0)
    {
        double total = 0.0, mn = g_fps_samples[0], mx = g_fps_samples[0];
        for (int i = 0; i < g_fps_samples_count; i++)
        {
            total += g_fps_samples[i];
            if (g_fps_samples[i] < mn) mn = g_fps_samples[i];
            if (g_fps_samples[i] > mx) mx = g_fps_samples[i];
        }
        printf("FPS samples collected: %d\n", g_fps_samples_count);
        printf("Windowed average FPS:  %.1f\n", total / g_fps_samples_count);
        printf("Windowed min FPS:       %.1f\n", mn);
        printf("Windowed max FPS:       %.1f\n", mx);
    }

    free(g_fps_samples);
    aroma_font_destroy(text_font);
    aroma_font_destroy(header_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}