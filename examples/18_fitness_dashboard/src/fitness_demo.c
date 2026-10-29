

#include "fitness_demo.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aroma_node.h"
#include "aroma_ui.h"
#include "widgets/aroma_canvas.h"
#include "widgets/aroma_card.h"
#include "widgets/aroma_icon.h"
#include "widgets/aroma_label.h"
#include "widgets/aroma_snackbar.h"
#include "widgets/aroma_textbox.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define C_WHITE 0xFFFFFFFFu
#define C_DARK 0xFF111111u
#define C_TEXT 0xFFE5E5E5u
#define C_MUTED 0xFFA3A3A3u
#define C_DIM 0xFF737373u
#define C_ORANGE 0xFFF97316u
#define C_ORANGE_LIGHT 0xFFFB923Cu
#define C_AMBER 0xFFF59E0Bu
#define C_YELLOW 0xFFFACC15u
#define C_GREEN 0xFF84CC16u
#define C_GREEN_BRIGHT 0xFF22C55Eu
#define C_RED 0xFFEF4444u
#define C_PINK 0xFFFB7185u
#define C_PANEL 0xFF1A1A1Au
#define C_PANEL_2 0xFF232323u
#define C_CHIP 0x4D000000u
#define C_CHIP_BORDER 0x33FFFFFFu
#define C_NAV_ICON 0xFFD4D4D4u

#define NAV_COUNT 5
#define PILL_COUNT 7

typedef struct FitnessDetail
{
    const char *kicker;
    const char *title;
    const char *value;
    const char *body1;
    const char *body2;
    const char *tag;
    int seed;
} FitnessDetail;

typedef struct FitnessMetric
{
    const char *label;
    const char *value;
} FitnessMetric;

static IncenseRegistry *s_registry;
static int s_nav;
static int s_page;
static int s_pill;
static int s_detail_seed;
static char s_ask_text[192];

static const char *const k_snacks[] = {
    "fitness_snack_nav0", "fitness_snack_nav1", "fitness_snack_nav2",
    "fitness_snack_nav3", "fitness_snack_nav4", "fitness_snack_quick",
    "fitness_snack_search", "fitness_snack_edit", "fitness_snack_date",
    "fitness_snack_wallet", "fitness_snack_report", "fitness_snack_pill",
    "fitness_snack_activity", "fitness_snack_sleep", "fitness_snack_heart",
    "fitness_snack_wellness", "fitness_snack_focus", "fitness_snack_energy",
    "fitness_snack_account", "fitness_snack_notifications",
    "fitness_snack_signout", "fitness_snack_ask",
    "fitness_snack_ask_empty", "fitness_snack_suggest",
    "fitness_snack_record",
};

static const char *const k_nav_snacks[NAV_COUNT] = {
    "fitness_snack_nav0", "fitness_snack_nav1", "fitness_snack_nav2",
    "fitness_snack_nav3", "fitness_snack_nav4",
};

static const char *const k_page_titles[3] = {
    "People", "Calendar", "Settings",
};

static const char *const k_page_rows[3][5][3] = {
    {
        {"Coach Maya Chen", "Strength and conditioning", "12 sessions"},
        {"Dr. Noah Williams", "Sports medicine", "2 reviews"},
        {"Lina Haddad", "Nutrition plan", "Updated"},
        {"Omar Farouk", "Recovery specialist", "Tomorrow"},
        {"Sara Kim", "Performance analyst", "Weekly"},
    },
    {
        {"Morning run", "Today - 06:30", "8.4 km"},
        {"Strength session", "Today - 18:00", "Upper body"},
        {"Mobility flow", "Tomorrow - 07:00", "20 min"},
        {"Recovery check", "Wed - 09:00", "Coach Maya"},
        {"Long ride", "Sat - 07:30", "60 km"},
    },
    {
        {"Training goal", "Build aerobic base", "On"},
        {"Weekly target", "5 sessions", "4 done"},
        {"Units", "Metric", "km / kg"},
        {"Notifications", "Workout reminders", "On"},
        {"Connected devices", "Watch and HR strap", "2 active"},
    },
};

static const FitnessDetail k_details[6] = {
    {"ACTIVITY", "Activity", "19,840", "Steps today",
     "18m from last week", "Daily movement", 1},
    {"SLEEP", "Sleep", "7h 45m", "Last night",
     "18m from last week", "Recovery", 2},
    {"HEART", "Heart", "63 BPM", "Resting average",
     "Within your normal range", "Cardio", 3},
    {"WELLNESS", "Wellness Score", "87", "Good condition",
     "+5 vs weekly average York", "Readiness", 4},
    {"FOCUS", "Focus Activity", "73", "Focus score",
     "Deep Work 14.5h", "Deep work", 5},
    {"ENERGY", "Energy & Recovery", "50%", "Balanced state",
     "Overall health stability index", "Stability", 6},
};

static const FitnessMetric k_metrics[PILL_COUNT] = {
    {"Speed", "50%"},
    {"Recovery", "72%"},
    {"Activity", "84%"},
    {"Energy", "63%"},
    {"Strength", "58%"},
    {"Heart", "61%"},
    {"Sleep", "76%"},
};

static const char *const k_suggestions[4] = {
    "How is my recovery?",
    "Plan a lighter session",
    "What should I eat today?",
    "Show my sleep trend",
};



static AromaNode *fitness_node(const char *id)
{
    return (s_registry && id) ? IncenseFindWidgetQuiet(s_registry, id) : NULL;
}

static bool fitness_visible(const char *id)
{
    AromaNode *n = fitness_node(id);
    return n && !aroma_node_is_hidden(n);
}

static void fitness_show(const char *id, bool visible)
{
    AromaNode *n = fitness_node(id);
    if (n)
        aroma_node_set_hidden(n, !visible);
}

static void fitness_text(const char *id, const char *text)
{
    AromaNode *n = fitness_node(id);
    if (n)
        aroma_label_set_text(n, text ? text : "");
}

static void fitness_text_color(const char *id, uint32_t color)
{
    AromaNode *n = fitness_node(id);
    if (n)
        aroma_label_set_color(n, color);
}

static void fitness_icon_color(const char *id, uint32_t color)
{
    AromaNode *n = fitness_node(id);
    if (n)
        aroma_icon_set_color(n, color);
}

static void fitness_card_colors(const char *id, uint32_t bg, uint32_t border)
{
    AromaNode *n = fitness_node(id);
    if (n && aroma_card_is_card(n))
        aroma_card_set_colors(n, bg, border);
}

static void fitness_dismiss_snacks(void)
{
    for (size_t i = 0; i < sizeof(k_snacks) / sizeof(k_snacks[0]); i++)
    {
        AromaNode *n = fitness_node(k_snacks[i]);
        if (n)
            aroma_snackbar_dismiss(n);
    }
}

static void fitness_toast(const char *id)
{
    fitness_dismiss_snacks();
    AromaNode *n = fitness_node(id);
    if (n)
        aroma_snackbar_show(n);
}

static void fitness_hide_modals(void)
{
    fitness_show("fitness_ask", false);
    fitness_show("fitness_detail", false);
    fitness_show("fitness_profile", false);
}



static void fitness_draw_polyline(AromaNode *c, const int (*pts)[2], int count,
                                  uint32_t color, int thickness)
{
    for (int i = 1; i < count; i++)
        aroma_canvas_draw_line(c, pts[i - 1][0], pts[i - 1][1],
                               pts[i][0], pts[i][1], color, thickness);
}

static void fitness_draw_activity(void)
{
    AromaNode *c = fitness_node("fitness_activity_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    static const int row_y[3] = {404, 424, 444};
    static const int counts[3] = {5, 7, 7};
    static const int xs[3][7] = {
        {160, 188, 224, 264, 292, 0, 0},
        {130, 158, 180, 214, 240, 272, 296},
        {130, 152, 178, 198, 226, 248, 278},
    };
    static const int ws[3][7] = {
        {18, 26, 30, 20, 14, 0, 0},
        {20, 14, 26, 18, 24, 16, 10},
        {14, 18, 12, 20, 14, 22, 16},
    };

    for (int row = 0; row < 3; row++)
    {
        for (int i = 0; i < counts[row]; i++)
        {
            uint32_t color = (i == counts[row] - 1) ? C_ORANGE_LIGHT
                                                    : C_ORANGE;
            aroma_canvas_draw_rect(c, xs[row][i], row_y[row],
                                   ws[row][i], 12, 3, true, color);
        }
    }
}

static void fitness_draw_sleep(void)
{
    AromaNode *c = fitness_node("fitness_sleep_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    static const int line0[6][2] = {
        {380, 448}, {420, 448}, {460, 442},
        {500, 424}, {560, 424}, {608, 424},
    };
    static const int line1[6][2] = {
        {380, 452}, {420, 452}, {460, 448},
        {500, 434}, {560, 434}, {608, 434},
    };
    static const int line2[6][2] = {
        {380, 456}, {420, 456}, {460, 452},
        {500, 442}, {560, 442}, {608, 442},
    };

    fitness_draw_polyline(c, line0, 6, C_GREEN, 2);
    fitness_draw_polyline(c, line1, 6, C_YELLOW, 2);
    fitness_draw_polyline(c, line2, 6, C_GREEN_BRIGHT, 2);
}

static void fitness_draw_heart(void)
{
    AromaNode *c = fitness_node("fitness_heart_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    static const int heights[7] = {18, 26, 22, 30, 24, 34, 20};
    aroma_canvas_draw_line(c, 660, 452, 914, 452, 0x66F59E0Bu, 1);
    for (int i = 0; i < 7; i++)
    {
        int x = 680 + i * 30;
        int h = heights[i];
        uint32_t color = (i == 6) ? C_ORANGE : C_AMBER;
        aroma_canvas_draw_rect(c, x, 452 - h, 18, h, 4, true, color);
    }
}

static void fitness_draw_wave(AromaNode *c, int x0, int x1, int base,
                              double amp, double period, double phase,
                              uint32_t color)
{
    const int segments = 16;
    int prev_x = x0;
    int prev_y = base + (int)(sin(phase) * amp);
    for (int i = 1; i <= segments; i++)
    {
        int x = x0 + (x1 - x0) * i / segments;
        int y = base + (int)(sin((double)(x - x0) / period + phase) * amp);
        aroma_canvas_draw_line(c, prev_x, prev_y, x, y, color, 2);
        prev_x = x;
        prev_y = y;
    }
}

static void fitness_draw_wellness(void)
{
    AromaNode *c = fitness_node("fitness_wellness_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    fitness_draw_wave(c, 64, 440, 690, 12.0, 26.0, 0.0, 0xCCF97316u);
    fitness_draw_wave(c, 64, 440, 692, 9.0, 31.0, 1.7, 0xCC84CC16u);
    fitness_draw_wave(c, 64, 440, 688, 6.0, 21.0, 3.1, 0xCC38BDF8u);
}

static void fitness_draw_focus(void)
{
    AromaNode *c = fitness_node("fitness_focus_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    const int count = 56;
    for (int i = 0; i < count; i++)
    {
        int x = 507 + i * 7;
        int h = 8 + ((i * 17 + 11) % 29);
        uint32_t color = (i >= 24 && i <= 31) ? C_ORANGE : 0xFF525252u;
        aroma_canvas_draw_rect(c, x, 710 - h, 3, h, 1, true, color);
    }
}

static void fitness_draw_gauge(void)
{
    AromaNode *c = fitness_node("fitness_gauge");
    if (!c)
        return;
    aroma_canvas_reset(c);

    const int cx = 1099;
    const int cy = 580;
    for (int i = 0; i < 44; i++)
    {
        double a = (-80.0 + 340.0 * (double)i / 43.0) * M_PI / 180.0;
        int x0 = cx + (int)(cos(a) * 78.0);
        int y0 = cy + (int)(sin(a) * 78.0);
        int x1 = cx + (int)(cos(a) * 86.0);
        int y1 = cy + (int)(sin(a) * 86.0);
        aroma_canvas_draw_line(c, x0, y0, x1, y1, 0xCCE5E5E5u, 2);
    }

    aroma_canvas_draw_line(c, 1099, 458, 1093, 470, C_WHITE, 2);
    aroma_canvas_draw_line(c, 1093, 470, 1105, 470, C_WHITE, 2);
    aroma_canvas_draw_line(c, 1105, 470, 1099, 458, C_WHITE, 2);
}

static void fitness_draw_waves(void)
{
    AromaNode *c = fitness_node("fitness_waves");
    if (!c)
        return;
    aroma_canvas_reset(c);

    const int segments = 24;
    const int x0 = 980;
    const int x1 = 1218;
    int prev_x = x0;
    int prev_y = 660 + (int)(sin(0.0) * 9.0);
    for (int i = 1; i <= segments; i++)
    {
        int x = x0 + (x1 - x0) * i / segments;
        int y = 660 + (int)(sin((double)(x - x0) / 28.0) * 9.0);
        aroma_canvas_draw_line(c, prev_x, prev_y, x, y, 0xCCF97316u, 2);
        prev_x = x;
        prev_y = y;
    }

    prev_x = x0;
    prev_y = 674 + (int)(sin(1.8) * 7.0);
    for (int i = 1; i <= segments; i++)
    {
        int x = x0 + (x1 - x0) * i / segments;
        int y = 674 + (int)(sin((double)(x - x0) / 22.0 + 1.8) * 7.0);
        aroma_canvas_draw_line(c, prev_x, prev_y, x, y, 0xCC84CC16u, 2);
        prev_x = x;
        prev_y = y;
    }
}

static void fitness_draw_detail_chart(void)
{
    AromaNode *c = fitness_node("fitness_detail_chart");
    if (!c)
        return;
    aroma_canvas_reset(c);

    
    aroma_canvas_draw_rect(c, 0, 0, 1, 1, 0, true, 0x00000000u);

    const int x0 = 372;
    const int x1 = 908;
    const int bottom = 516;
    const int count = 12;
    const int step = (x1 - x0) / count;
    int prev_x = 0;
    int prev_y = 0;

    for (int i = 0; i < count; i++)
    {
        int x = x0 + i * step;
        int h = 40 + ((i * 37 + s_detail_seed * 19) % 100);
        int y = bottom - h;
        uint32_t fill = (i == 5 || i == 6) ? 0x66F97316u : 0x33F97316u;
        aroma_canvas_draw_rect(c, x, y, step - 12, h, 6, true, fill);
        if (i > 0)
            aroma_canvas_draw_line(c, prev_x, prev_y, x + 16, y,
                                   C_ORANGE, 2);
        prev_x = x + 16;
        prev_y = y;
    }
    aroma_canvas_draw_circle(c, prev_x, prev_y, 4, C_ORANGE);
}



static void fitness_set_nav(int index)
{
    if (index < 0 || index >= NAV_COUNT)
        index = 0;
    s_nav = index;
    for (int i = 0; i < NAV_COUNT; i++)
    {
        bool active = (i == index);
        char id[40];
        snprintf(id, sizeof(id), "fitness_nav_bg%d", i);
        fitness_card_colors(id, active ? C_WHITE : C_CHIP,
                            active ? C_WHITE : C_CHIP_BORDER);
        snprintf(id, sizeof(id), "fitness_nav_icon%d", i);
        fitness_icon_color(id, active ? C_DARK : C_NAV_ICON);
    }
}

static void fitness_set_pill(int index)
{
    if (index < 0 || index >= PILL_COUNT)
        index = 0;
    s_pill = index;
    for (int i = 0; i < PILL_COUNT; i++)
    {
        bool active = (i == index);
        char id[40];
        snprintf(id, sizeof(id), "fitness_pill_bg%d", i);
        fitness_card_colors(id, active ? C_WHITE : C_CHIP,
                            active ? C_WHITE : C_CHIP_BORDER);
        snprintf(id, sizeof(id), "fitness_pill_icon%d", i);
        fitness_icon_color(id, active ? C_DARK : C_NAV_ICON);
    }
    fitness_text("fitness_gauge_label", k_metrics[index].label);
    fitness_text("fitness_gauge_value", k_metrics[index].value);
}

static void fitness_fill_page(int page)
{
    if (page < 0 || page > 2)
        page = 0;
    s_page = page;
    fitness_text("fitness_page_title", k_page_titles[page]);
    char crumb[64];
    snprintf(crumb, sizeof(crumb), "Overview / %s", k_page_titles[page]);
    fitness_text("fitness_page_crumb", crumb);

    for (int i = 0; i < 5; i++)
    {
        char id[48];
        snprintf(id, sizeof(id), "fitness_page_row%d_title", i);
        fitness_text(id, k_page_rows[page][i][0]);
        snprintf(id, sizeof(id), "fitness_page_row%d_meta", i);
        fitness_text(id, k_page_rows[page][i][1]);
        snprintf(id, sizeof(id), "fitness_page_row%d_value", i);
        fitness_text(id, k_page_rows[page][i][2]);
    }
}

static void fitness_apply_nav(int index)
{
    if (index < 0 || index >= NAV_COUNT)
        index = 0;

    if (index == 1)
    {
        fitness_show("fitness_page", false);
        fitness_set_nav(1);
        fitness_show("fitness_ask", true);
        AromaNode *box = fitness_node("fitness_ask_input");
        if (box)
        {
            aroma_textbox_set_text(box, "");
            aroma_textbox_set_focused(box, true);
        }
        s_ask_text[0] = '\0';
        fitness_text("fitness_ask_status",
                     "Try a suggestion or ask your own question.");
        fitness_toast(k_nav_snacks[index]);
        return;
    }

    if (index >= 2)
    {
        fitness_hide_modals();
        fitness_set_nav(index);
        fitness_fill_page(index - 2);
        fitness_show("fitness_page", true);
        fitness_toast(k_nav_snacks[index]);
        return;
    }

    fitness_show("fitness_page", false);
    fitness_hide_modals();
    fitness_set_nav(0);
    fitness_toast(k_nav_snacks[0]);
}

static void fitness_open_ask(void)
{
    fitness_hide_modals();
    fitness_set_nav(1);
    AromaNode *box = fitness_node("fitness_ask_input");
    if (box)
    {
        aroma_textbox_set_text(box, "");
        aroma_textbox_set_focused(box, true);
    }
    s_ask_text[0] = '\0';
    fitness_text("fitness_ask_status",
                 "Try a suggestion or ask your own question.");
    fitness_show("fitness_ask", true);
}

static void fitness_open_detail(int index)
{
    if (index < 0 || index >= 6)
        index = 0;
    const FitnessDetail *d = &k_details[index];
    fitness_hide_modals();
    fitness_text("fitness_detail_kicker", d->kicker);
    fitness_text("fitness_detail_title", d->title);
    fitness_text("fitness_detail_value", d->value);
    fitness_text("fitness_detail_body1", d->body1);
    fitness_text("fitness_detail_body2", d->body2);
    fitness_text("fitness_detail_tag", d->tag);
    s_detail_seed = d->seed;
    fitness_draw_detail_chart();
    fitness_show("fitness_detail", true);
}



static void fitness_reg_void(const char *name, void (*fn)(void *), void *ud)
{
    IncenseRegisterCallback(name, INCENSE_CALLBACK_VOID_PTR, (void *)fn, ud);
}

static void fitness_cb_nav(void *userdata)
{
    fitness_apply_nav((int)(intptr_t)userdata);
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_home(void *userdata)
{
    (void)userdata;
    fitness_apply_nav(0);
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_page_back(void *userdata)
{
    (void)userdata;
    fitness_apply_nav(0);
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_page_row(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    if (s_page < 0 || s_page > 2 || index < 0 || index >= 5)
        return;
    const char *title = k_page_rows[s_page][index][0];
    const char *meta = k_page_rows[s_page][index][1];
    const char *value = k_page_rows[s_page][index][2];
    fitness_hide_modals();
    fitness_text("fitness_detail_kicker", "RECORD");
    fitness_text("fitness_detail_title", title);
    fitness_text("fitness_detail_value", value);
    fitness_text("fitness_detail_body1", meta);
    fitness_text("fitness_detail_body2",
                 "This record is rendered by the live AromaUI tree.");
    fitness_text("fitness_detail_tag", k_page_titles[s_page]);
    s_detail_seed = index + 10;
    fitness_draw_detail_chart();
    fitness_show("fitness_detail", true);
    fitness_toast("fitness_snack_record");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_ask_open(void *userdata)
{
    (void)userdata;
    fitness_open_ask();
    fitness_toast("fitness_snack_search");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_ask_close(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_ask", false);
    fitness_set_nav(0);
    aroma_ui_request_redraw(NULL);
}

static bool fitness_cb_ask_change(AromaNode *node, const char *text, void *ud)
{
    (void)node;
    (void)ud;
    snprintf(s_ask_text, sizeof(s_ask_text), "%s", text ? text : "");
    return true;
}

static void fitness_cb_suggest(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    if (index < 0 || index >= 4)
        return;
    AromaNode *box = fitness_node("fitness_ask_input");
    if (box)
    {
        aroma_textbox_set_text(box, k_suggestions[index]);
        aroma_textbox_set_focused(box, true);
    }
    snprintf(s_ask_text, sizeof(s_ask_text), "%s", k_suggestions[index]);
    fitness_text("fitness_ask_status", "Suggestion loaded. Press Ask coach.");
    fitness_toast("fitness_snack_suggest");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_ask_send(void *userdata)
{
    (void)userdata;
    if (!s_ask_text[0])
    {
        fitness_toast("fitness_snack_ask_empty");
        return;
    }
    fitness_text("fitness_ask_status", "Coach is reviewing your question...");
    fitness_toast("fitness_snack_ask");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_quick_add(void *userdata)
{
    (void)userdata;
    fitness_toast("fitness_snack_quick");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_search(void *userdata)
{
    (void)userdata;
    fitness_open_ask();
    fitness_toast("fitness_snack_search");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_edit(void *userdata)
{
    (void)userdata;
    fitness_toast("fitness_snack_edit");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_date(void *userdata)
{
    (void)userdata;
    fitness_toast("fitness_snack_date");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_add_wallet(void *userdata)
{
    (void)userdata;
    fitness_toast("fitness_snack_wallet");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_create_report(void *userdata)
{
    (void)userdata;
    fitness_toast("fitness_snack_report");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_profile_open(void *userdata)
{
    (void)userdata;
    if (fitness_visible("fitness_profile"))
    {
        fitness_show("fitness_profile", false);
    }
    else
    {
        fitness_hide_modals();
        fitness_show("fitness_profile", true);
    }
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_profile_close(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_profile", false);
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_account(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_profile", false);
    fitness_toast("fitness_snack_account");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_notifications(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_profile", false);
    fitness_toast("fitness_snack_notifications");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_signout(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_profile", false);
    fitness_toast("fitness_snack_signout");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_activity(void *userdata)
{
    (void)userdata;
    fitness_open_detail(0);
    fitness_toast("fitness_snack_activity");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_sleep(void *userdata)
{
    (void)userdata;
    fitness_open_detail(1);
    fitness_toast("fitness_snack_sleep");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_heart(void *userdata)
{
    (void)userdata;
    fitness_open_detail(2);
    fitness_toast("fitness_snack_heart");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_wellness(void *userdata)
{
    (void)userdata;
    fitness_open_detail(3);
    fitness_toast("fitness_snack_wellness");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_wellness_insights(void *userdata)
{
    (void)userdata;
    fitness_open_detail(3);
    fitness_toast("fitness_snack_wellness");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_focus(void *userdata)
{
    (void)userdata;
    fitness_open_detail(4);
    fitness_toast("fitness_snack_focus");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_focus_insights(void *userdata)
{
    (void)userdata;
    fitness_open_detail(4);
    fitness_toast("fitness_snack_focus");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_energy(void *userdata)
{
    (void)userdata;
    fitness_open_detail(5);
    fitness_toast("fitness_snack_energy");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_pill(void *userdata)
{
    fitness_set_pill((int)(intptr_t)userdata);
    fitness_toast("fitness_snack_pill");
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_detail_close(void *userdata)
{
    (void)userdata;
    fitness_show("fitness_detail", false);
    aroma_ui_request_redraw(NULL);
}

static void fitness_cb_noop(void *userdata)
{
    (void)userdata;
}



void fitness_register_callbacks(void)
{
    for (int i = 0; i < NAV_COUNT; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "fitness_nav%d", i);
        fitness_reg_void(name, fitness_cb_nav, (void *)(intptr_t)i);
    }
    fitness_reg_void("fitness_home", fitness_cb_home, NULL);
    fitness_reg_void("fitness_page_back", fitness_cb_page_back, NULL);
    for (int i = 0; i < 5; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "fitness_page_row%d", i);
        fitness_reg_void(name, fitness_cb_page_row, (void *)(intptr_t)i);
    }

    fitness_reg_void("fitness_ask_open", fitness_cb_ask_open, NULL);
    fitness_reg_void("fitness_ask_close", fitness_cb_ask_close, NULL);
    IncenseRegisterCallback("fitness_ask_change",
                            INCENSE_CALLBACK_NODE_STRING_PTR,
                            (void *)fitness_cb_ask_change, NULL);
    for (int i = 0; i < 4; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "fitness_suggest%d", i);
        fitness_reg_void(name, fitness_cb_suggest, (void *)(intptr_t)i);
    }
    fitness_reg_void("fitness_ask_send", fitness_cb_ask_send, NULL);

    fitness_reg_void("fitness_quick_add", fitness_cb_quick_add, NULL);
    fitness_reg_void("fitness_search", fitness_cb_search, NULL);
    fitness_reg_void("fitness_edit", fitness_cb_edit, NULL);
    fitness_reg_void("fitness_date", fitness_cb_date, NULL);
    fitness_reg_void("fitness_add_wallet", fitness_cb_add_wallet, NULL);
    fitness_reg_void("fitness_create_report", fitness_cb_create_report, NULL);

    fitness_reg_void("fitness_profile_open", fitness_cb_profile_open, NULL);
    fitness_reg_void("fitness_profile_close", fitness_cb_profile_close, NULL);
    fitness_reg_void("fitness_account", fitness_cb_account, NULL);
    fitness_reg_void("fitness_notifications", fitness_cb_notifications, NULL);
    fitness_reg_void("fitness_signout", fitness_cb_signout, NULL);

    fitness_reg_void("fitness_activity", fitness_cb_activity, NULL);
    fitness_reg_void("fitness_sleep", fitness_cb_sleep, NULL);
    fitness_reg_void("fitness_heart", fitness_cb_heart, NULL);
    fitness_reg_void("fitness_wellness", fitness_cb_wellness, NULL);
    fitness_reg_void("fitness_wellness_insights",
                     fitness_cb_wellness_insights, NULL);
    fitness_reg_void("fitness_focus", fitness_cb_focus, NULL);
    fitness_reg_void("fitness_focus_insights", fitness_cb_focus_insights,
                     NULL);
    fitness_reg_void("fitness_energy", fitness_cb_energy, NULL);

    for (int i = 0; i < PILL_COUNT; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "fitness_pill%d", i);
        fitness_reg_void(name, fitness_cb_pill, (void *)(intptr_t)i);
    }

    fitness_reg_void("fitness_detail_close", fitness_cb_detail_close, NULL);
    fitness_reg_void("fitness_noop", fitness_cb_noop, NULL);
}

void fitness_after_mount(IncenseRegistry *registry)
{
    s_registry = registry;
    s_nav = 0;
    s_page = 0;
    s_pill = 0;
    s_detail_seed = 0;
    s_ask_text[0] = '\0';

    if (!fitness_node("fitness_root"))
        return;

    fitness_hide_modals();
    fitness_show("fitness_page", false);
    fitness_dismiss_snacks();
    fitness_set_nav(0);
    fitness_set_pill(0);

    fitness_draw_activity();
    fitness_draw_sleep();
    fitness_draw_heart();
    fitness_draw_wellness();
    fitness_draw_focus();
    fitness_draw_gauge();
    fitness_draw_waves();
    fitness_draw_detail_chart();
    aroma_ui_request_redraw(NULL);
}
