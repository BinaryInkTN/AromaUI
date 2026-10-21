/* Loud dashboard runtime for the docs landing-page demo.
 *
 * The Incense markup in docs/sandbox.html is intentionally only a view.
 * This file owns the application state behind it: navigation, period
 * selection, transactions, filters, notifications, settings, overlays and
 * the small amount of CSV export glue that turns the demo into something
 * that behaves like a real dashboard instead of a screenshot.
 */

#include "loud_dashboard.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aroma_ui.h"
#include "aroma_incense_loader.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define LOUD_NAV 5
#define LOUD_MAX_TXNS 16
#define LOUD_TABLE_ROWS 8
#define LOUD_SEARCH_ROWS 5
#define LOUD_PERIOD_COUNT 3
#define LOUD_NOTIF_COUNT 4

#define COL_TEXT 0xFFFFFFFFu
#define COL_MUTED 0xFFA6A6ABu
#define COL_DIM 0xFF77777Du
#define COL_INACTIVE 0xFFB9B9BEu
#define COL_PILL 0xFF1A1A1Cu
#define COL_PILL_ACTIVE 0xFFFFFFFFu
#define COL_BLACK 0xFF000000u
#define COL_PERIOD_IDLE 0xFF1A1A1Cu
#define COL_PERIOD_ACTIVE 0xFF2E2E30u
#define COL_LIME 0xFFC9F25Au
#define COL_VIOLET 0xFF8B5CF6u
#define COL_BLUE 0xFF3F9FF6u
#define COL_AMBER 0xFFF5B94Bu
#define COL_RED 0xFFFF9CAEu
#define COL_ROW 0xFF242426u
#define COL_ROW_DONE 0xFF1E2A17u
#define COL_ROW_PENDING 0xFF2E2414u
#define COL_ROW_REVIEW 0xFF172436u

static const uint32_t k_heat_colors[5] = {
    0xFF2F224Eu, 0xFF4F2E97u, 0xFF743BEAu, 0xFF9060F0u, 0xFFAC8CF1u};

static void db_download_text(const char *filename, const char *content);
static void db_sync_switches(void);
static void db_reset_heat_colors(void);
static void db_set_initial_inputs(void);
static void db_set_error(const char *id, const char *message);

/* ------------------------------------------------------------------ state */

typedef struct {
    char name[64];
    char category[24];
    long amount_cents;
    char date[24];
    int status; /* 0 completed, 1 pending, 2 reviewed */
    uint32_t dot;
} LoudTxn;

typedef struct {
    const double *income;
    const double *expenses;
    int points;
    const char *const *labels;
    int label_count;
    const char *delta;
    const char *invest_delta;
    const char *products_delta;
    long revenue;
    long invest;
    long products;
    const char *other;
    const char *date_start;
    const char *date_end;
    const char *report_label;
    const int *bar_products;
    const int *bar_other;
    int invest_width;
} LoudPeriod;

static IncenseRegistry *s_registry;

static AromaFont *s_sized_fonts[16];
static int s_sized_px[16];
static int s_sized_count;

static int s_nav = 1;
static int s_period = 1;
static long s_balance_cents = 1695700;
static bool s_hide_money;
static bool s_prefs[3] = {true, true, false};
static bool s_notif_read[LOUD_NOTIF_COUNT];
static int s_txn_filter;
static char s_txn_query[96];
static char s_search_query[96];
static char s_profile_name[64] = "Angela Martin";
static char s_profile_email[96] = "angela@loud.app";
static int s_detail_index = -1;
static int s_confirm_action;
static int s_heat_sel = -1;
static bool s_syncing;

static LoudTxn s_txns[LOUD_MAX_TXNS];
static int s_txn_count;
static int s_tab_map[LOUD_TABLE_ROWS];
static int s_tab_count;
static int s_search_map[LOUD_SEARCH_ROWS];
static int s_search_count;

static const double k_income_week[7] = {38, 45, 52, 48, 61, 72, 58};
static const double k_expense_week[7] = {22, 26, 24, 30, 35, 29, 26};
static const double k_income_month[31] = {
    46, 50, 43, 36, 33, 36, 43, 51, 63, 79, 93, 97, 89, 71, 53, 39,
    36, 43, 57, 63, 59, 49, 45, 47, 53, 49, 43, 39, 41, 45, 48};
static const double k_expense_month[31] = {
    22, 19, 21, 26, 30, 27, 19, 17, 21, 28, 32, 37, 39, 35, 31, 27,
    23, 20, 23, 28, 26, 20, 18, 22, 27, 32, 29, 23, 20, 22, 24};
static const double k_income_year[12] = {42, 47, 55, 63, 58, 66, 74, 81, 76, 88, 95, 91};
static const double k_expense_year[12] = {25, 28, 30, 34, 29, 32, 38, 42, 36, 44, 47, 45};

static const char *const k_week_labels[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static const char *const k_month_labels[6] = {
    "Mar", "Apr", "May", "Jun", "Jul", "Aug"};
static const char *const k_year_labels[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static const int k_products_week[20] = {
    22, 27, 19, 31, 25, 34, 30, 24, 28, 32, 21, 29, 26, 33, 24, 30, 27, 35, 23, 29};
static const int k_products_month[20] = {
    31, 28, 35, 32, 26, 24, 31, 34, 28, 37, 33, 27, 29, 31, 26, 28, 34, 30, 27, 32};
static const int k_products_year[20] = {
    33, 37, 30, 28, 35, 39, 32, 26, 31, 36, 34, 28, 30, 38, 29, 33, 35, 31, 27, 36};
static const int k_other_week[8] = {21, 19, 26, 23, 18, 25, 22, 20};
static const int k_other_month[8] = {29, 25, 32, 27, 24, 30, 26, 23};
static const int k_other_year[8] = {27, 31, 24, 29, 33, 26, 28, 35};

static const LoudPeriod k_periods[LOUD_PERIOD_COUNT] = {
    {
        k_income_week, k_expense_week, 7, k_week_labels, 7,
        "+8.42%", "+6.10%", "+4.35%",
        418200, 124500, 228640, "0.74",
        "August 17", "August 23", "Week of Aug 17",
        k_products_week, k_other_week, 214,
    },
    {
        k_income_month, k_expense_month, 31, k_month_labels, 6,
        "+12.67%", "+23%", "+12%",
        1695700, 446500, 845870, "1.24",
        "January 26", "February 26", "August 2026",
        k_products_month, k_other_month, 290,
    },
    {
        k_income_year, k_expense_year, 12, k_year_labels, 12,
        "+24.10%", "+18.40%", "+9.26%",
        19842000, 5236000, 9641000, "1.86",
        "January 26", "December 26", "FY 2026",
        k_products_year, k_other_year, 252,
    },
};

/* --------------------------------------------------------------- plumbing */

static void db_trace(const char *name)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.__aromaLastCb = UTF8ToString($0); }, name);
#else
    (void)name;
#endif
}

static AromaNode *db_node(const char *id)
{
    return (s_registry && id) ? IncenseFindWidgetQuiet(s_registry, id) : NULL;
}

static void db_show(const char *id, bool show)
{
    AromaNode *n = db_node(id);
    if (n)
        aroma_node_set_hidden(n, !show);
}

static void db_set_text(const char *id, const char *text)
{
    AromaNode *n = db_node(id);
    if (n)
        aroma_label_set_text(n, text ? text : "");
}

static void db_set_label_color(const char *id, uint32_t color)
{
    AromaNode *n = db_node(id);
    if (n)
        aroma_label_set_color(n, color);
}

static void db_set_card_color(const char *id, uint32_t color)
{
    AromaNode *n = db_node(id);
    if (n)
        aroma_card_set_colors(n, color, color);
}

static void db_set_x(const char *id, int x)
{
    AromaNode *n = db_node(id);
    if (!n)
        return;
    AromaRect *r = aroma_node_get_rect(n);
    if (r && r->x != x) {
        r->x = x;
        aroma_node_invalidate(n);
    }
}

static void db_set_rect(const char *id, int x, int y, int w, int h)
{
    AromaNode *n = db_node(id);
    if (!n)
        return;
    AromaRect *r = aroma_node_get_rect(n);
    if (!r)
        return;
    r->x = x;
    r->y = y;
    r->width = w;
    r->height = h;
    aroma_node_invalidate(n);
}

static void db_set_textbox(const char *id, const char *text)
{
    AromaNode *n = db_node(id);
    if (n)
        aroma_textbox_set_text(n, text ? text : "");
}

static const char *db_textbox_text(const char *id)
{
    AromaNode *n = db_node(id);
    return n ? aroma_textbox_get_text(n) : "";
}

static void db_focus_textbox(const char *id)
{
    AromaNode *n = db_node(id);
    if (!n)
        return;
    aroma_textbox_set_focused(n, true);
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var c = (Module && Module.canvas) || document.getElementById('canvas');
        if (c) {
            if (!c.hasAttribute('tabindex'))
                c.setAttribute('tabindex', '0');
            c.style.outline = 'none';
            if (c.focus)
                c.focus();
        }
    });
#endif
}

static const char *const k_snackbars[] = {
    "snack_transfer", "snack_request", "snack_report", "snack_receipt",
    "snack_saved", "snack_prefs", "snack_privacy_on", "snack_privacy_off",
    "snack_reviewed", "snack_reset", "snack_chart_export", "snack_welcome",
    "snack_heat", "snack_invalid"};

static void db_dismiss_toasts(void)
{
    for (size_t i = 0; i < sizeof(k_snackbars) / sizeof(k_snackbars[0]); i++)
    {
        AromaNode *n = db_node(k_snackbars[i]);
        if (n)
            aroma_snackbar_dismiss(n);
    }
}

static void db_toast(const char *id)
{
    db_dismiss_toasts();
    AromaNode *n = db_node(id);
    if (n)
        aroma_snackbar_show(n);
}

static int db_text_width(const char *text, int px)
{
    if (!text)
        return 0;
    for (int i = 0; i < s_sized_count; i++) {
        if (s_sized_px[i] == px && s_sized_fonts[i])
            return aroma_font_get_line_width(s_sized_fonts[i], text);
    }
    return (int)strlen(text) * px * 3 / 5;
}

static void db_center_label(const char *id, int center_x, int px)
{
    AromaNode *n = db_node(id);
    if (!n)
        return;
    const char *text = aroma_label_get_text(n);
    if (!text)
        return;
    db_set_x(id, center_x - db_text_width(text, px) / 2);
}

static void db_group_dollars(long dollars, char *out, size_t cap)
{
    char digits[32];
    snprintf(digits, sizeof(digits), "%ld", dollars);
    size_t len = strlen(digits);
    size_t j = 0;
    for (size_t i = 0; i < len && j + 2 < cap; i++) {
        if (i > 0 && (len - i) % 3 == 0)
            out[j++] = ',';
        out[j++] = digits[i];
    }
    out[j] = '\0';
}

static void db_money_raw(long cents, bool signed_amount, char *out, size_t cap)
{
    long value = cents < 0 ? -cents : cents;
    char dollars[40];
    db_group_dollars(value / 100, dollars, sizeof(dollars));
    const char *sign = "";
    if (signed_amount)
        sign = cents > 0 ? "+" : (cents < 0 ? "-" : "");
    else if (cents < 0)
        sign = "-";
    snprintf(out, cap, "%s$%s.%02ld", sign, dollars, value % 100);
}

static void db_money(long cents, bool signed_amount, char *out, size_t cap)
{
    if (s_hide_money) {
        snprintf(out, cap, "••••");
        return;
    }
    db_money_raw(cents, signed_amount, out, cap);
}

static void db_buf_append(char *buf, size_t cap, const char *fmt, ...)
{
    size_t used = strlen(buf);
    if (used >= cap - 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + used, cap - used, fmt, ap);
    va_end(ap);
}

static bool db_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    if (!haystack)
        return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return true;
    }
    return false;
}

static void db_elide(char *dst, size_t cap, const char *src, size_t max_chars)
{
    if (!dst || cap == 0)
        return;
    size_t len = src ? strlen(src) : 0;
    if (len <= max_chars || max_chars < 4) {
        snprintf(dst, cap, "%s", src ? src : "");
        return;
    }
    size_t keep = max_chars - 3;
    if (keep >= cap)
        keep = cap - 1;
    memcpy(dst, src, keep);
    dst[keep] = '\0';
    strncat(dst, "...", cap - strlen(dst) - 1);
}

static void db_trim_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    while (*src && isspace((unsigned char)*src))
        src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1]))
        len--;
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static const char *db_status_text(int status)
{
    switch (status) {
    case 1: return "Pending";
    case 2: return "Reviewed";
    default: return "Completed";
    }
}

static uint32_t db_status_color(int status)
{
    switch (status) {
    case 1: return COL_AMBER;
    case 2: return COL_BLUE;
    default: return COL_LIME;
    }
}

static uint32_t db_status_row_color(int status)
{
    switch (status) {
    case 1: return COL_ROW_PENDING;
    case 2: return COL_ROW_REVIEW;
    default: return COL_ROW_DONE;
    }
}

/* ------------------------------------------------------------------ data */

static void db_txn_set(int i, const char *name, const char *category,
                       long amount_cents, const char *date, int status,
                       uint32_t dot)
{
    if (i < 0 || i >= LOUD_MAX_TXNS)
        return;
    LoudTxn *t = &s_txns[i];
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "");
    snprintf(t->category, sizeof(t->category), "%s", category ? category : "Other");
    snprintf(t->date, sizeof(t->date), "%s", date ? date : "");
    t->amount_cents = amount_cents;
    t->status = status;
    t->dot = dot;
}

static void db_seed_transactions(void)
{
    s_txn_count = 0;
    db_txn_set(s_txn_count++, "Internet", "Multimedia", -4000,
               "Aug 23, 2026", 0, COL_LIME);
    db_txn_set(s_txn_count++, "Isabella Garcia", "Transfer", -8650,
               "Aug 22, 2026", 0, COL_VIOLET);
    db_txn_set(s_txn_count++, "Sephora", "Beauty", -24880,
               "Aug 21, 2026", 0, 0xFF31B6C8u);
    db_txn_set(s_txn_count++, "Netflix", "Multimedia", -24880,
               "Aug 20, 2026", 0, COL_LIME);
    db_txn_set(s_txn_count++, "Violet Oream", "Transfer", 50000,
               "Aug 19, 2026", 0, COL_VIOLET);
    db_txn_set(s_txn_count++, "Spotify", "Multimedia", -1199,
               "Aug 18, 2026", 0, COL_LIME);
    db_txn_set(s_txn_count++, "Payroll", "Income", 420000,
               "Aug 15, 2026", 0, COL_LIME);
    db_txn_set(s_txn_count++, "Amazon", "Shopping", -9214,
               "Aug 14, 2026", 0, COL_AMBER);
    db_txn_set(s_txn_count++, "Transfer to savings", "Transfer", -100000,
               "Aug 12, 2026", 0, COL_VIOLET);
    db_txn_set(s_txn_count++, "Freelance invoice", "Income", 185000,
               "Aug 10, 2026", 1, COL_LIME);
}

static void db_insert_txn(const char *name, const char *category,
                          long amount_cents, int status, uint32_t dot)
{
    if (s_txn_count < LOUD_MAX_TXNS)
        s_txn_count++;
    for (int i = s_txn_count - 1; i > 0; i--)
        s_txns[i] = s_txns[i - 1];
    db_txn_set(0, name, category, amount_cents, "Just now", status, dot);
}

static long db_pending_cents(void)
{
    long total = 0;
    for (int i = 0; i < s_txn_count; i++) {
        if (s_txns[i].amount_cents > 0 && s_txns[i].status == 1)
            total += s_txns[i].amount_cents;
    }
    return total;
}

static int db_pending_count(void)
{
    int count = 0;
    for (int i = 0; i < s_txn_count; i++) {
        if (s_txns[i].amount_cents > 0 && s_txns[i].status == 1)
            count++;
    }
    return count;
}

static void db_reset_data(bool reset_view)
{
    s_balance_cents = 1695700;
    s_hide_money = false;
    s_prefs[0] = true;
    s_prefs[1] = true;
    s_prefs[2] = false;
    memset(s_notif_read, 0, sizeof(s_notif_read));
    s_txn_filter = 0;
    s_txn_query[0] = '\0';
    s_search_query[0] = '\0';
    snprintf(s_profile_name, sizeof(s_profile_name), "Angela Martin");
    snprintf(s_profile_email, sizeof(s_profile_email), "angela@loud.app");
    s_detail_index = -1;
    s_confirm_action = 0;
    s_heat_sel = -1;
    db_seed_transactions();
    if (reset_view) {
        s_nav = 1;
        s_period = 1;
    }
}

/* ---------------------------------------------------------------- charts */

static void db_chart_series(const char *id, const double *ys, int count)
{
    AromaNode *n = db_node(id);
    if (!n || count <= 0)
        return;
    aroma_chart_set_series(n, NULL, ys, (size_t)count);
    aroma_chart_set_range(n, false, 0.0, (double)(count - 1), true, 0.0, 100.0);
}

static void db_chart_labels(const char *prefix, int x0, int width,
                            const char *const *labels, int count)
{
    for (int i = 0; i < 12; i++) {
        char id[24];
        snprintf(id, sizeof(id), "%s%d", prefix, i);
        if (i >= count) {
            db_show(id, false);
            continue;
        }
        const char *text = labels[i];
        db_set_text(id, text);
        db_show(id, true);
        int w = db_text_width(text, 12);
        int x = x0 + (count > 1 ? (i * width) / (count - 1) : 0) - w / 2;
        db_set_x(id, x);
    }
}

/* --------------------------------------------------------------- metrics */

static void db_update_privacy_status(void)
{
    db_set_text("settings_privacy_status",
                s_hide_money ? "Balances are hidden" : "Balances are visible");
    db_set_label_color("settings_privacy_status",
                       s_hide_money ? COL_AMBER : COL_LIME);
}

static void db_update_prefs_status(void)
{
    int enabled = 0;
    for (int i = 0; i < 3; i++)
        if (s_prefs[i])
            enabled++;
    char text[64];
    snprintf(text, sizeof(text), "%d of 3 alerts enabled", enabled);
    db_set_text("settings_pref_status", text);
    db_set_label_color("settings_pref_status", enabled ? COL_LIME : COL_MUTED);
}

static void db_update_profile_labels(void)
{
    db_set_text("profile_name", s_profile_name);
    db_set_text("profile_email", s_profile_email);
    db_set_text("settings_account", s_profile_email);

    char first[48];
    snprintf(first, sizeof(first), "%s", s_profile_name);
    char *space = strchr(first, ' ');
    if (space)
        *space = '\0';
    char greeting[96];
    snprintf(greeting, sizeof(greeting), "Welcome back, %s",
             first[0] ? first : "friend");
    db_set_text("welcome_heading", greeting);
}

static void db_update_metrics(void)
{
    const LoudPeriod *p = &k_periods[s_period];

    char full[64];
    db_money_raw(p->revenue, false, full, sizeof(full));
    char main_part[64];
    char cents_part[16] = "";
    if (s_hide_money) {
        snprintf(main_part, sizeof(main_part), "••••");
    } else {
        char *dot = strrchr(full, '.');
        if (dot) {
            snprintf(cents_part, sizeof(cents_part), "%s", dot);
            *dot = '\0';
            snprintf(main_part, sizeof(main_part), "%s", full);
        } else {
            snprintf(main_part, sizeof(main_part), "%s", full);
        }
    }
    db_set_text("revenue_main", main_part);
    db_set_text("revenue_cents", cents_part);
    db_set_text("revenue_delta", p->delta);

    int main_w = db_text_width(main_part, 40);
    int cents_w = s_hide_money ? 0 : db_text_width(cents_part, 40);
    db_set_x("revenue_cents", 24 + main_w + 2);
    db_set_x("revenue_delta", 24 + main_w + 2 + cents_w + 12);

    char money[64];
    db_money(s_balance_cents, false, money, sizeof(money));
    db_set_text("available_value", money);
    db_set_text("dash_available", money);
    db_set_text("transfer_available", money);

    db_money(p->revenue, false, money, sizeof(money));
    db_set_text("dash_revenue", money);
    char sub[96];
    snprintf(sub, sizeof(sub), "%s vs last period", p->delta);
    db_set_text("dash_sub1", sub);
    db_set_text("stat_invest_delta", p->invest_delta);
    db_set_text("stat_products_delta", p->products_delta);
    db_set_x("stat_invest_delta", 480 - db_text_width(p->invest_delta, 12));
    db_set_x("stat_products_delta", 816 - db_text_width(p->products_delta, 12));
    db_set_text("stat_other_value", p->other);

    db_money(p->invest, false, money, sizeof(money));
    db_set_text("stat_invest_value", money);
    db_money(p->products, false, money, sizeof(money));
    db_set_text("stat_products_value", money);

    long pending = db_pending_cents();
    int pending_count = db_pending_count();
    db_money(pending, false, money, sizeof(money));
    db_set_text("dash_pending", money);
    char pending_sub[64];
    snprintf(pending_sub, sizeof(pending_sub), "%d request%s awaiting payment",
             pending_count, pending_count == 1 ? "" : "s");
    db_set_text("dash_sub2", pending_sub);

    db_money(796800, false, money, sizeof(money));
    db_set_text("chart_callout_income", money);
    db_money(595700, false, money, sizeof(money));
    db_set_text("chart_callout_expense", money);

    db_set_text("dash_sub0", "Across 2 accounts");
    db_set_text("settings_privacy_status",
                s_hide_money ? "Balances are hidden" : "Balances are visible");
}

static void db_apply_bars(void)
{
    const LoudPeriod *p = &k_periods[s_period];
    for (int i = 0; i < 20; i++) {
        char id[24];
        snprintf(id, sizeof(id), "product_bar_%d", i);
        int h = p->bar_products[i];
        db_set_rect(id, 784 + (i * 33) / 2, 390 - h, 7, h);
    }
    for (int i = 0; i < 8; i++) {
        char id[24];
        snprintf(id, sizeof(id), "other_bar_%d", i);
        int h = p->bar_other[i];
        db_set_rect(id, 1136 + i * 13, 390 - h, 7, h);
    }
    db_set_rect("invest_fill", 448, 346, p->invest_width, 44);
}

static void db_apply_period(void)
{
    const LoudPeriod *p = &k_periods[s_period];
    static const char *const ids[3] = {
        "period_bg_week", "period_bg_month", "period_bg_year"};
    static const char *const labels[3] = {
        "period_lb_week", "period_lb_month", "period_lb_year"};

    for (int i = 0; i < 3; i++) {
        bool active = i == s_period;
        db_set_card_color(ids[i], active ? COL_PERIOD_ACTIVE : COL_PERIOD_IDLE);
        db_set_label_color(labels[i], active ? COL_TEXT : COL_MUTED);
    }

    db_chart_series("income_chart", p->income, p->points);
    db_chart_series("expense_chart", p->expenses, p->points);
    db_chart_series("dash_income_chart", p->income, p->points);
    db_chart_series("dash_expense_chart", p->expenses, p->points);

    db_chart_labels("chart_x", 50, 352, p->labels, p->label_count);
    db_chart_labels("dash_x", 52, 752, p->labels, p->label_count);
    db_show("chart_marker", s_period == 1);

    db_set_text("bars_date_start", p->date_start);
    db_set_text("bars_date_end", p->date_end);
    db_set_text("report_period", p->report_label);

    db_apply_bars();
    db_update_metrics();
    aroma_ui_request_redraw(NULL);
}

/* ---------------------------------------------------------------- render */

static void db_render_recent(void)
{
    for (int i = 0; i < 5; i++) {
        char id[24];
        if (i >= s_txn_count) {
            snprintf(id, sizeof(id), "txn_name%d", i); db_show(id, false);
            snprintf(id, sizeof(id), "txn_pill%d", i); db_show(id, false);
            snprintf(id, sizeof(id), "txn_dot%d", i); db_show(id, false);
            snprintf(id, sizeof(id), "txn_cat%d", i); db_show(id, false);
            snprintf(id, sizeof(id), "txn_amt%d", i); db_show(id, false);
            continue;
        }
        LoudTxn *t = &s_txns[i];
        char name[48];
        db_elide(name, sizeof(name), t->name, 20);
        snprintf(id, sizeof(id), "txn_name%d", i); db_set_text(id, name);
        snprintf(id, sizeof(id), "txn_cat%d", i); db_set_text(id, t->category);
        uint32_t color = t->dot;
        snprintf(id, sizeof(id), "txn_dot%d", i);
        db_set_card_color(id, color);
        char amount[40];
        db_money(t->amount_cents, true, amount, sizeof(amount));
        snprintf(id, sizeof(id), "txn_amt%d", i);
        db_set_text(id, amount);
        db_set_x(id, 1222 - db_text_width(amount, 14));
        db_show(id, true);
        snprintf(id, sizeof(id), "txn_name%d", i); db_show(id, true);
        snprintf(id, sizeof(id), "txn_pill%d", i); db_show(id, true);
        snprintf(id, sizeof(id), "txn_dot%d", i); db_show(id, true);
        snprintf(id, sizeof(id), "txn_cat%d", i); db_show(id, true);
    }
}

static bool db_txn_matches(const LoudTxn *t, const char *query, int filter)
{
    if (filter == 1 && t->amount_cents <= 0)
        return false;
    if (filter == 2 && t->amount_cents >= 0)
        return false;
    if (filter == 3 && strcmp(t->category, "Transfer") != 0 &&
        strcmp(t->category, "Request") != 0)
        return false;
    if (query && query[0]) {
        char amount[40];
        db_money_raw(t->amount_cents, true, amount, sizeof(amount));
        if (!db_contains_ci(t->name, query) &&
            !db_contains_ci(t->category, query) &&
            !db_contains_ci(t->date, query) &&
            !db_contains_ci(amount, query))
            return false;
    }
    return true;
}

static void db_table_row_show(int i, bool show)
{
    static const char *const names[] = {
        "tab_name", "tab_pill", "tab_dot", "tab_cat", "tab_date",
        "tab_status_pill", "tab_status", "tab_amt", "tab_hit"};
    char id[32];
    for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
        snprintf(id, sizeof(id), "%s%d", names[k], i);
        db_show(id, show);
    }
}

static void db_render_transactions(void)
{
    static const char *const filter_ids[4] = {
        "txn_filter_bg0", "txn_filter_bg1", "txn_filter_bg2", "txn_filter_bg3"};
    static const char *const filter_labels[4] = {
        "txn_filter_lb0", "txn_filter_lb1", "txn_filter_lb2", "txn_filter_lb3"};
    for (int i = 0; i < 4; i++) {
        bool active = i == s_txn_filter;
        db_set_card_color(filter_ids[i], active ? COL_PILL_ACTIVE : COL_PILL);
        db_set_label_color(filter_labels[i], active ? COL_BLACK : COL_INACTIVE);
    }

    s_tab_count = 0;
    for (int i = 0; i < s_txn_count && s_tab_count < LOUD_TABLE_ROWS; i++) {
        LoudTxn *t = &s_txns[i];
        if (!db_txn_matches(t, s_txn_query, s_txn_filter))
            continue;
        int row = s_tab_count++;
        s_tab_map[row] = i;

        char id[24];
        char name[64];
        db_elide(name, sizeof(name), t->name, 28);
        snprintf(id, sizeof(id), "tab_name%d", row); db_set_text(id, name);
        snprintf(id, sizeof(id), "tab_cat%d", row); db_set_text(id, t->category);
        snprintf(id, sizeof(id), "tab_date%d", row); db_set_text(id, t->date);
        snprintf(id, sizeof(id), "tab_status%d", row); db_set_text(id, db_status_text(t->status));
        snprintf(id, sizeof(id), "tab_status_pill%d", row);
        db_set_card_color(id, db_status_row_color(t->status));
        snprintf(id, sizeof(id), "tab_status%d", row);
        db_set_label_color(id, db_status_color(t->status));
        snprintf(id, sizeof(id), "tab_dot%d", row);
        db_set_card_color(id, t->dot);

        char amount[40];
        db_money(t->amount_cents, true, amount, sizeof(amount));
        snprintf(id, sizeof(id), "tab_amt%d", row);
        db_set_text(id, amount);
        db_set_x(id, 1232 - db_text_width(amount, 14));
        db_set_label_color(id, t->amount_cents > 0 ? COL_LIME : COL_TEXT);
        db_table_row_show(row, true);
    }

    for (int row = s_tab_count; row < LOUD_TABLE_ROWS; row++)
        db_table_row_show(row, false);
    db_show("txn_empty", s_tab_count == 0);

    char count[96];
    snprintf(count, sizeof(count), "Showing %d of %d transactions",
             s_tab_count, s_txn_count);
    db_set_text("txn_count", count);
}

static void db_render_search(void)
{
    s_search_count = 0;
    for (int i = 0; i < s_txn_count && s_search_count < LOUD_SEARCH_ROWS; i++) {
        LoudTxn *t = &s_txns[i];
        if (!db_contains_ci(t->name, s_search_query) &&
            !db_contains_ci(t->category, s_search_query) &&
            !db_contains_ci(t->date, s_search_query))
            continue;
        int row = s_search_count++;
        s_search_map[row] = i;
        char id[32];
        char name[64];
        db_elide(name, sizeof(name), t->name, 30);
        snprintf(id, sizeof(id), "search_name%d", row); db_set_text(id, name);
        char meta[96];
        snprintf(meta, sizeof(meta), "%s  ·  %s", t->category, t->date);
        snprintf(id, sizeof(id), "search_meta%d", row); db_set_text(id, meta);
        char amount[40];
        db_money(t->amount_cents, true, amount, sizeof(amount));
        snprintf(id, sizeof(id), "search_amt%d", row); db_set_text(id, amount);
        db_set_x(id, 890 - db_text_width(amount, 14));
        db_set_label_color(id, t->amount_cents > 0 ? COL_LIME : COL_TEXT);
        snprintf(id, sizeof(id), "search_res%d", row); db_show(id, true);
    }
    for (int row = s_search_count; row < LOUD_SEARCH_ROWS; row++) {
        char id[24];
        snprintf(id, sizeof(id), "search_res%d", row);
        db_show(id, false);
    }
    db_show("search_empty", s_search_count == 0);
}

static void db_update_notifications(void)
{
    bool all_read = true;
    for (int i = 0; i < LOUD_NOTIF_COUNT; i++) {
        char id[24];
        snprintf(id, sizeof(id), "notif_dot%d", i);
        db_show(id, !s_notif_read[i]);
        if (!s_notif_read[i])
            all_read = false;
    }
    db_show("mail_badge", !all_read);
}

static void db_render_dashboard(void)
{
    for (int i = 0; i < 3; i++) {
        char id[24];
        if (i >= s_txn_count)
            continue;
        LoudTxn *t = &s_txns[i];
        char name[48];
        db_elide(name, sizeof(name), t->name, 19);
        snprintf(id, sizeof(id), "dash_txn_name%d", i); db_set_text(id, name);
        char amount[40];
        db_money(t->amount_cents, true, amount, sizeof(amount));
        snprintf(id, sizeof(id), "dash_txn_amt%d", i); db_set_text(id, amount);
        db_set_x(id, 1216 - db_text_width(amount, 12));
        db_set_label_color(id, t->amount_cents > 0 ? COL_LIME : COL_MUTED);
    }
}

/* --------------------------------------------------------------- overlays */

static const char *const k_overlays[] = {
    "overlay_search", "overlay_notifications", "overlay_profile",
    "overlay_transfer", "overlay_request", "overlay_txn_detail",
    "overlay_quick", "overlay_chart_menu", "overlay_confirm",
    "overlay_signedout"};

static void db_hide_overlays(void)
{
    for (size_t i = 0; i < sizeof(k_overlays) / sizeof(k_overlays[0]); i++)
        db_show(k_overlays[i], false);

    static const char *const boxes[] = {
        "search_input", "txn_search_input", "transfer_recipient",
        "transfer_amount", "request_payer", "request_amount",
        "settings_name", "settings_email"};
    for (size_t i = 0; i < sizeof(boxes) / sizeof(boxes[0]); i++) {
        AromaNode *n = db_node(boxes[i]);
        if (n)
            aroma_textbox_set_focused(n, false);
    }
    s_confirm_action = 0;
}

static void db_show_overlay(const char *which)
{
    db_dismiss_toasts();
    db_hide_overlays();
    db_show(which, true);
    aroma_ui_request_redraw(NULL);
}

static void db_update_detail(void)
{
    if (s_detail_index < 0 || s_detail_index >= s_txn_count)
        return;
    LoudTxn *t = &s_txns[s_detail_index];
    db_set_text("detail_name", t->name);
    char amount[40];
    db_money(t->amount_cents, true, amount, sizeof(amount));
    db_set_text("detail_amount", amount);
    db_set_text("detail_category", t->category);
    db_set_text("detail_date", t->date);
    db_set_text("detail_status", db_status_text(t->status));
    db_set_label_color("detail_status", db_status_color(t->status));
    db_set_text("detail_method",
                (strcmp(t->category, "Transfer") == 0 ||
                 strcmp(t->category, "Request") == 0)
                    ? "Checking  •••• 4821"
                    : "Visa  •••• 4242");
    char id[32];
    snprintf(id, sizeof(id), "LD-2026-%04d", 1042 + s_detail_index);
    db_set_text("detail_id", id);
}

static void db_open_detail(int index)
{
    if (index < 0 || index >= s_txn_count)
        return;
    s_detail_index = index;
    db_show_overlay("overlay_txn_detail");
    db_update_detail();
}

static void db_apply_nav(void)
{
    static const char *const pages[LOUD_NAV] = {
        "page_dashboard", "page_analytics", "page_transactions",
        "page_reports", "page_settings"};
    static const char *const nav_bg[LOUD_NAV] = {
        "nav_bg0", "nav_bg1", "nav_bg2", "nav_bg3", "nav_bg4"};
    static const char *const nav_lb[LOUD_NAV] = {
        "nav_lb0", "nav_lb1", "nav_lb2", "nav_lb3", "nav_lb4"};

    for (int i = 0; i < LOUD_NAV; i++) {
        db_show(pages[i], i == s_nav);
        bool active = i == s_nav;
        db_set_card_color(nav_bg[i], active ? COL_PILL_ACTIVE : COL_PILL);
        db_set_label_color(nav_lb[i], active ? COL_BLACK : COL_INACTIVE);
    }
    bool period_visible = s_nav == 0 || s_nav == 1 || s_nav == 3;
    db_show("period_control", period_visible);

    if (s_nav == 2)
        db_render_transactions();
    if (s_nav == 0)
        db_render_dashboard();
    if (s_nav == 3)
        db_set_text("report_period", k_periods[s_period].report_label);
    aroma_ui_request_redraw(NULL);
}

static void db_nav_to(int index)
{
    if (index < 0 || index >= LOUD_NAV)
        return;
    s_nav = index;
    db_hide_overlays();
    db_apply_nav();
}

/* --------------------------------------------------------------- controls */

static void db_nav_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    char trace[32];
    snprintf(trace, sizeof(trace), "dashboard_nav_%d", index);
    db_trace(trace);
    db_nav_to(index);
}

static void db_period_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    static const char *const names[3] = {
        "dashboard_period_week", "dashboard_period_month", "dashboard_period_year"};
    db_trace(names[index]);
    s_period = index;
    db_apply_period();
}

static void db_search_open_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_search_open");
    db_show_overlay("overlay_search");
    s_search_query[0] = '\0';
    db_set_textbox("search_input", "");
    db_render_search();
    db_focus_textbox("search_input");
}

static bool db_search_change_cb(AromaNode *node, const char *text, void *userdata)
{
    (void)node;
    (void)userdata;
    db_trace("dashboard_search_change");
    db_trim_copy(s_search_query, sizeof(s_search_query), text);
    db_render_search();
    aroma_ui_request_redraw(NULL);
    return true;
}

static void db_search_hit_cb(void *userdata)
{
    int row = (int)(intptr_t)userdata;
    if (row < 0 || row >= s_search_count)
        return;
    db_trace("dashboard_search_hit");
    db_open_detail(s_search_map[row]);
}

static void db_notifications_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_notifications");
    db_update_notifications();
    db_show_overlay("overlay_notifications");
}

static void db_notif_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    char trace[32];
    snprintf(trace, sizeof(trace), "dashboard_notif_%d", index);
    db_trace(trace);
    if (index >= 0 && index < LOUD_NOTIF_COUNT)
        s_notif_read[index] = true;
    db_update_notifications();
    static const int destinations[4] = {2, 3, 1, 4};
    db_nav_to(destinations[index]);
}

static void db_notifications_read_all_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_notifications_read_all");
    for (int i = 0; i < LOUD_NOTIF_COUNT; i++)
        s_notif_read[i] = true;
    db_update_notifications();
}

static void db_profile_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_profile");
    db_update_profile_labels();
    db_show_overlay("overlay_profile");
}

static void db_profile_settings_cb(void *userdata)
{
    (void)userdata;
    db_nav_to(4);
}

static void db_profile_billing_cb(void *userdata)
{
    (void)userdata;
    db_nav_to(3);
}

static void db_profile_signout_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_profile_signout");
    db_show_overlay("overlay_confirm");
    s_confirm_action = 1;
    db_set_text("confirm_title", "Sign out of Loud?");
    db_set_text("confirm_message", "Your demo session will reset.");
}

static void db_quick_menu_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_quick_menu");
    db_show_overlay("overlay_quick");
}

static void db_chart_menu_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_chart_menu");
    db_show_overlay("overlay_chart_menu");
}

static void db_close_overlay_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_close_overlay");
    db_hide_overlays();
}

static void db_noop_cb(void *userdata)
{
    (void)userdata;
}

static void db_goto_analytics_cb(void *userdata)
{
    (void)userdata;
    db_nav_to(1);
}

static void db_goto_transactions_cb(void *userdata)
{
    (void)userdata;
    db_nav_to(2);
}

static void db_goto_reports_cb(void *userdata)
{
    (void)userdata;
    db_nav_to(3);
}

static void db_reset_confirm_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_reset_confirm");
    db_show_overlay("overlay_confirm");
    s_confirm_action = 2;
    db_set_text("confirm_title", "Reset demo data?");
    db_set_text("confirm_message", "Restore the sample transactions and preferences?");
}

static void db_signout_confirm_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_signout_confirm");
    db_show_overlay("overlay_confirm");
    s_confirm_action = 1;
    db_set_text("confirm_title", "Sign out of Loud?");
    db_set_text("confirm_message", "Your demo session will reset.");
}

static void db_confirm_yes_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_confirm_yes");
    int action = s_confirm_action;
    s_confirm_action = 0;
    if (action == 1) {
        db_show_overlay("overlay_signedout");
    } else if (action == 2) {
        db_reset_data(true);
        db_hide_overlays();
        db_set_initial_inputs();
        db_set_text("settings_status", "");
        db_set_error("transfer_error", NULL);
        db_set_error("request_error", NULL);
        db_sync_switches();
        db_update_profile_labels();
        db_update_prefs_status();
        db_update_privacy_status();
        db_update_notifications();
        db_render_recent();
        db_render_transactions();
        db_render_search();
        db_render_dashboard();
        db_reset_heat_colors();
        db_apply_nav();
        db_apply_period();
        db_render_dashboard();
        db_toast("snack_reset");
    }
}

static void db_sign_back_in_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_sign_back_in");
    db_reset_data(true);
    db_hide_overlays();
    db_set_initial_inputs();
    db_set_text("settings_status", "");
    db_set_error("transfer_error", NULL);
    db_set_error("request_error", NULL);
    db_sync_switches();
    db_update_profile_labels();
    db_update_prefs_status();
    db_update_privacy_status();
    db_update_notifications();
    db_render_recent();
    db_render_transactions();
    db_render_search();
    db_reset_heat_colors();
    db_apply_nav();
    db_apply_period();
    db_render_dashboard();
    db_toast("snack_welcome");
}

/* --------------------------------------------------------------- settings */

static void db_switch_sync_guard(bool enable)
{
    s_syncing = enable;
}

static bool db_switch_privacy_cb(AromaNode *node, void *userdata)
{
    (void)userdata;
    bool hidden = aroma_switch_get_state(node);
    s_hide_money = hidden;
    db_update_privacy_status();
    db_update_metrics();
    db_render_recent();
    db_render_transactions();
    db_render_search();
    db_render_dashboard();
    if (s_detail_index >= 0)
        db_update_detail();
    if (!s_syncing)
        db_toast(hidden ? "snack_privacy_on" : "snack_privacy_off");
    aroma_ui_request_redraw(NULL);
    return true;
}

static bool db_switch_pref_cb(AromaNode *node, void *userdata)
{
    int index = (int)(intptr_t)userdata;
    if (index < 0 || index > 2)
        return false;
    s_prefs[index] = aroma_switch_get_state(node);
    db_update_prefs_status();
    if (!s_syncing)
        db_toast("snack_prefs");
    aroma_ui_request_redraw(NULL);
    return true;
}

static void db_settings_save_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_settings_save");
    char name[64];
    char email[96];
    db_trim_copy(name, sizeof(name), db_textbox_text("settings_name"));
    db_trim_copy(email, sizeof(email), db_textbox_text("settings_email"));

    if (!name[0]) {
        db_set_text("settings_status", "Enter a display name.");
        db_set_label_color("settings_status", COL_RED);
        db_toast("snack_invalid");
        return;
    }
    if (!strchr(email, '@') || !strchr(email, '.')) {
        db_set_text("settings_status", "Enter a valid email address.");
        db_set_label_color("settings_status", COL_RED);
        db_toast("snack_invalid");
        return;
    }

    snprintf(s_profile_name, sizeof(s_profile_name), "%s", name);
    snprintf(s_profile_email, sizeof(s_profile_email), "%s", email);
    db_update_profile_labels();
    db_set_text("settings_status", "Saved just now.");
    db_set_label_color("settings_status", COL_LIME);
    db_toast("snack_saved");
}

/* ----------------------------------------------------------- transactions */

static void db_txn_filter_cb(void *userdata)
{
    int filter = (int)(intptr_t)userdata;
    if (filter < 0 || filter > 3)
        return;
    s_txn_filter = filter;
    db_render_transactions();
    aroma_ui_request_redraw(NULL);
}

static bool db_txn_search_cb(AromaNode *node, const char *text, void *userdata)
{
    (void)node;
    (void)userdata;
    db_trace("dashboard_txn_search");
    db_trim_copy(s_txn_query, sizeof(s_txn_query), text);
    db_render_transactions();
    aroma_ui_request_redraw(NULL);
    return true;
}

static void db_txn_row_cb(void *userdata)
{
    int row = (int)(intptr_t)userdata;
    if (row < 0 || row >= s_tab_count)
        return;
    db_open_detail(s_tab_map[row]);
}

static void db_txn_recent_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    db_open_detail(index);
}

static void db_dash_txn_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    db_open_detail(index);
}

static void db_detail_receipt_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_detail_receipt");
    if (s_detail_index < 0 || s_detail_index >= s_txn_count)
        return;
    LoudTxn *t = &s_txns[s_detail_index];
    char amount[40];
    db_money_raw(t->amount_cents, true, amount, sizeof(amount));
    char csv[1024];
    snprintf(csv, sizeof(csv),
             "Date,Name,Category,Amount,Status,Transaction ID\n"
             "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"LD-2026-%04d\"\n",
             t->date, t->name, t->category, amount,
             db_status_text(t->status), 1042 + s_detail_index);
    char filename[64];
    snprintf(filename, sizeof(filename), "loud-receipt-%04d.csv",
             1042 + s_detail_index);
    db_download_text(filename, csv);
    db_toast("snack_receipt");
}

static void db_detail_review_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_detail_review");
    if (s_detail_index < 0 || s_detail_index >= s_txn_count)
        return;
    s_txns[s_detail_index].status = 2;
    db_update_detail();
    db_render_recent();
    db_render_transactions();
    db_update_metrics();
    db_render_dashboard();
    db_toast("snack_reviewed");
}

/* --------------------------------------------------------------- transfer */

static bool db_parse_amount(const char *text, long *out_cents)
{
    char clean[64];
    int j = 0;
    for (const char *p = text; *p && j + 1 < (int)sizeof(clean); p++) {
        if ((*p >= '0' && *p <= '9') || *p == '.')
            clean[j++] = *p;
    }
    clean[j] = '\0';
    if (!clean[0])
        return false;
    char *end = NULL;
    double value = strtod(clean, &end);
    if (end == clean || *end != '\0' || value <= 0.0 || value > 1000000.0)
        return false;
    *out_cents = (long)(value * 100.0 + 0.5);
    return *out_cents > 0;
}

static void db_set_error(const char *id, const char *message)
{
    if (message && message[0]) {
        db_set_text(id, message);
        db_show(id, true);
    } else {
        db_set_text(id, "");
        db_show(id, false);
    }
}

static void db_open_transfer_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_transfer");
    db_show_overlay("overlay_transfer");
    db_set_textbox("transfer_recipient", "");
    db_set_textbox("transfer_amount", "");
    db_set_error("transfer_error", NULL);
    db_update_metrics();
    db_focus_textbox("transfer_recipient");
}

static void db_open_request_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_request");
    db_show_overlay("overlay_request");
    db_set_textbox("request_payer", "");
    db_set_textbox("request_amount", "");
    db_set_error("request_error", NULL);
    db_focus_textbox("request_payer");
}

static void db_quick_amount_cb(void *userdata, const char *box)
{
    int index = (int)(intptr_t)userdata;
    static const char *const values[3] = {"50.00", "100.00", "500.00"};
    db_set_textbox(box, values[index]);
    db_set_error(box[0] == 't' ? "transfer_error" : "request_error", NULL);
    db_focus_textbox(box);
}

static void db_transfer_amount_cb(void *userdata)
{
    db_quick_amount_cb(userdata, "transfer_amount");
}

static void db_request_amount_cb(void *userdata)
{
    db_quick_amount_cb(userdata, "request_amount");
}

static void db_transfer_submit_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_transfer_submit");
    char recipient[64];
    db_trim_copy(recipient, sizeof(recipient),
                 db_textbox_text("transfer_recipient"));
    long cents = 0;
    if (!recipient[0]) {
        db_set_error("transfer_error", "Enter a recipient.");
        db_toast("snack_invalid");
        return;
    }
    if (!db_parse_amount(db_textbox_text("transfer_amount"), &cents)) {
        db_set_error("transfer_error", "Enter an amount greater than zero.");
        db_toast("snack_invalid");
        return;
    }
    if (cents > s_balance_cents) {
        db_set_error("transfer_error", "Amount exceeds your available balance.");
        db_toast("snack_invalid");
        return;
    }

    char name[64];
    db_elide(name, sizeof(name), recipient, 26);
    db_insert_txn(name, "Transfer", -cents, 1, COL_VIOLET);
    s_balance_cents -= cents;
    db_hide_overlays();
    db_render_recent();
    db_render_transactions();
    db_render_search();
    db_update_metrics();
    db_render_dashboard();
    db_toast("snack_transfer");
    aroma_ui_request_redraw(NULL);
}

static void db_request_submit_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_request_submit");
    char payer[64];
    db_trim_copy(payer, sizeof(payer), db_textbox_text("request_payer"));
    long cents = 0;
    if (!payer[0]) {
        db_set_error("request_error", "Enter who should pay.");
        db_toast("snack_invalid");
        return;
    }
    if (!db_parse_amount(db_textbox_text("request_amount"), &cents)) {
        db_set_error("request_error", "Enter an amount greater than zero.");
        db_toast("snack_invalid");
        return;
    }

    char name[64];
    db_elide(name, sizeof(name), payer, 26);
    db_insert_txn(name, "Request", cents, 1, COL_VIOLET);
    db_hide_overlays();
    db_render_recent();
    db_render_transactions();
    db_render_search();
    db_update_metrics();
    db_render_dashboard();
    db_toast("snack_request");
    aroma_ui_request_redraw(NULL);
}

/* ------------------------------------------------------------------ heat */

static const int k_heat_levels[6][7] = {
    {0, 0, 0, 1, 0, 0, 0},
    {0, 0, 2, 3, 3, 0, 0},
    {0, 3, 2, 2, 2, 3, 0},
    {1, 1, 2, 4, 2, 2, 1},
    {0, 1, 1, 2, 4, 0, 0},
    {0, 0, 0, 1, 0, 0, 0},
};
static const int k_heat_counts[6][7] = {
    {1, 2, 2, 5, 2, 1, 1},
    {2, 3, 8, 13, 12, 3, 2},
    {3, 12, 9, 10, 9, 14, 3},
    {5, 6, 11, 18, 10, 9, 4},
    {2, 4, 6, 8, 15, 3, 2},
    {1, 1, 2, 5, 1, 1, 0},
};
static const char *const k_heat_days[7] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static const char *const k_heat_times[6] = {
    "1pm", "2pm", "3pm", "4pm", "5pm", "6pm"};

static void db_reset_heat_colors(void)
{
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            int index = row * 7 + col;
            char id[24];
            snprintf(id, sizeof(id), "heat_%d", index);
            db_set_card_color(id, k_heat_colors[k_heat_levels[row][col]]);
        }
    }
    s_heat_sel = -1;
    db_set_text("heat_info", "Peak Thu 4pm");
    db_set_label_color("heat_info", COL_LIME);
}

static void db_heat_cb(void *userdata)
{
    int index = (int)(intptr_t)userdata;
    if (index < 0 || index >= 42)
        return;
    db_trace("dashboard_heat");
    if (s_heat_sel >= 0 && s_heat_sel < 42) {
        int old_row = s_heat_sel / 7;
        int old_col = s_heat_sel % 7;
        char id[24];
        snprintf(id, sizeof(id), "heat_%d", s_heat_sel);
        db_set_card_color(id, k_heat_colors[k_heat_levels[old_row][old_col]]);
    }
    int row = index / 7;
    int col = index % 7;
    s_heat_sel = index;
    char id[24];
    snprintf(id, sizeof(id), "heat_%d", index);
    uint32_t base = k_heat_colors[k_heat_levels[row][col]];
    db_set_card_color(id, aroma_color_adjust(base, 0.16f));

    char info[64];
    snprintf(info, sizeof(info), "%s %s - %d actions",
             k_heat_days[col], k_heat_times[row], k_heat_counts[row][col]);
    db_set_text("heat_info", info);
    db_set_label_color("heat_info", COL_TEXT);
    aroma_ui_request_redraw(NULL);
}

/* ---------------------------------------------------------------- export */

#ifdef __EMSCRIPTEN__
static void db_download_text(const char *filename, const char *content)
{
    EM_ASM({
        try {
            var blob = new Blob([UTF8ToString($1)], {type: 'text/csv;charset=utf-8'});
            var a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = UTF8ToString($0);
            document.body.appendChild(a);
            a.click();
            setTimeout(function() {
                URL.revokeObjectURL(a.href);
                a.remove();
            }, 1500);
        } catch (e) {
            console.error('Loud download failed', e);
        }
    }, filename, content);
}
#else
static void db_download_text(const char *filename, const char *content)
{
    if (!filename || !content)
        return;
    FILE *file = fopen(filename, "wb");
    if (!file)
        return;
    fwrite(content, 1, strlen(content), file);
    fclose(file);
}
#endif

static void db_csv_append_txn(char *csv, size_t cap, const LoudTxn *t, int index)
{
    char amount[40];
    db_money_raw(t->amount_cents, true, amount, sizeof(amount));
    db_buf_append(csv, cap,
                  "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"LD-2026-%04d\"\n",
                  t->date, t->name, t->category, amount,
                  db_status_text(t->status), 1042 + index);
}

static void db_export_transactions_csv(const char *filename, bool filtered,
                                       bool with_summary)
{
    char csv[16384];
    memset(csv, 0, sizeof(csv));
    if (with_summary) {
        char available[64];
        char revenue[64];
        db_money_raw(s_balance_cents, false, available, sizeof(available));
        db_money_raw(k_periods[s_period].revenue, false, revenue, sizeof(revenue));
        db_buf_append(csv, sizeof(csv),
                      "Loud statement\nPeriod,\"%s\"\nAvailable,\"%s\"\nRevenue,\"%s\"\n\n",
                      k_periods[s_period].report_label, available, revenue);
    }
    db_buf_append(csv, sizeof(csv),
                  "Date,Name,Category,Amount,Status,Transaction ID\n");
    if (filtered) {
        for (int i = 0; i < s_txn_count; i++) {
            if (db_txn_matches(&s_txns[i], s_txn_query, s_txn_filter))
                db_csv_append_txn(csv, sizeof(csv), &s_txns[i], i);
        }
    } else {
        for (int i = 0; i < s_txn_count; i++)
            db_csv_append_txn(csv, sizeof(csv), &s_txns[i], i);
    }
    db_download_text(filename, csv);
}

static void db_export_chart_csv(void)
{
    const LoudPeriod *p = &k_periods[s_period];
    char csv[4096];
    memset(csv, 0, sizeof(csv));
    db_buf_append(csv, sizeof(csv), "Point,Income,Expenses\n");
    for (int i = 0; i < p->points; i++)
        db_buf_append(csv, sizeof(csv), "%d,%.2f,%.2f\n", i,
                      p->income[i], p->expenses[i]);
    db_download_text("loud-chart-data.csv", csv);
}

static void db_export_heat_csv(void)
{
    char csv[8192];
    memset(csv, 0, sizeof(csv));
    db_buf_append(csv, sizeof(csv), "Day,Time,Level,Actions\n");
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            db_buf_append(csv, sizeof(csv), "%s,%s,%d,%d\n",
                          k_heat_days[col], k_heat_times[row],
                          k_heat_levels[row][col], k_heat_counts[row][col]);
        }
    }
    db_download_text("loud-activity.csv", csv);
}

static void db_export_category_csv(bool income)
{
    const char *names[16];
    long totals[16];
    int count = 0;
    for (int i = 0; i < s_txn_count; i++) {
        LoudTxn *t = &s_txns[i];
        if (income ? t->amount_cents <= 0 : t->amount_cents >= 0)
            continue;
        int found = -1;
        for (int k = 0; k < count; k++) {
            if (strcmp(names[k], t->category) == 0) {
                found = k;
                break;
            }
        }
        if (found < 0 && count < 16) {
            found = count++;
            names[found] = t->category;
            totals[found] = 0;
        }
        if (found >= 0)
            totals[found] += t->amount_cents;
    }
    char csv[4096];
    memset(csv, 0, sizeof(csv));
    db_buf_append(csv, sizeof(csv), "Category,Amount\n");
    for (int i = 0; i < count; i++) {
        char amount[40];
        db_money_raw(totals[i], false, amount, sizeof(amount));
        db_buf_append(csv, sizeof(csv), "\"%s\",\"%s\"\n", names[i], amount);
    }
    db_download_text(income ? "loud-income-by-category.csv"
                            : "loud-expenses-by-category.csv", csv);
}

static void db_export_account_csv(void)
{
    char csv[16384];
    memset(csv, 0, sizeof(csv));
    char balance[64];
    db_money_raw(s_balance_cents, false, balance, sizeof(balance));
    db_buf_append(csv, sizeof(csv),
                  "Loud account export\nName,\"%s\"\nEmail,\"%s\"\n"
                  "Period,\"%s\"\nAvailable,\"%s\"\n\n",
                  s_profile_name, s_profile_email,
                  k_periods[s_period].report_label, balance);
    db_buf_append(csv, sizeof(csv),
                  "Date,Name,Category,Amount,Status,Transaction ID\n");
    for (int i = 0; i < s_txn_count; i++)
        db_csv_append_txn(csv, sizeof(csv), &s_txns[i], i);
    db_download_text("loud-account-data.csv", csv);
}

static void db_report_cb(void *userdata)
{
    int which = (int)(intptr_t)userdata;
    char trace[32];
    snprintf(trace, sizeof(trace), "dashboard_report_%d", which);
    db_trace(trace);
    switch (which) {
    case 0:
        db_export_transactions_csv("loud-statement.csv", false, true);
        break;
    case 1:
        db_export_category_csv(true);
        break;
    case 2:
        db_export_category_csv(false);
        break;
    case 3:
        db_export_heat_csv();
        break;
    default:
        break;
    }
    db_toast("snack_report");
}

static void db_export_heatmap_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_export_heatmap");
    db_export_heat_csv();
    db_toast("snack_heat");
}

static void db_export_chart_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_export_chart");
    db_export_chart_csv();
    db_hide_overlays();
    db_toast("snack_chart_export");
}

static void db_export_transactions_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_export_transactions");
    db_export_transactions_csv("loud-transactions.csv", true, false);
    db_toast("snack_report");
}

static void db_export_statement_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_export_statement");
    db_export_transactions_csv("loud-statement.csv", false, true);
    db_toast("snack_report");
}

static void db_export_account_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_export_account");
    db_export_account_csv();
    db_hide_overlays();
    db_toast("snack_report");
}

static void db_reset_period_cb(void *userdata)
{
    (void)userdata;
    db_trace("dashboard_reset_period");
    s_period = 1;
    db_hide_overlays();
    db_apply_period();
}

/* ---------------------------------------------------------------- wiring */

void loud_dashboard_bind_fonts(AromaFont **fonts, const int *sizes, int count)
{
    if (!fonts || !sizes)
        return;
    s_sized_count = count > 16 ? 16 : count;
    for (int i = 0; i < s_sized_count; i++) {
        s_sized_fonts[i] = fonts[i];
        s_sized_px[i] = sizes[i];
    }
}

void loud_dashboard_reset(void)
{
    s_registry = NULL;
    db_reset_data(true);
}

static void db_style_textboxes(void)
{
    static const char *const ids[] = {
        "search_input", "txn_search_input", "transfer_recipient",
        "transfer_amount", "request_payer", "request_amount",
        "settings_name", "settings_email"};
    AromaFont *font = NULL;
    for (int i = 0; i < s_sized_count; i++)
        if (s_sized_px[i] == 16)
            font = s_sized_fonts[i];

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        AromaNode *n = db_node(ids[i]);
        if (!n || !n->node_widget_ptr)
            continue;
        AromaTextbox *tb = (AromaTextbox *)n->node_widget_ptr;
        tb->use_theme_colors = false;
        tb->bg_color = 0xFF101012u;
        tb->hover_bg_color = 0xFF17171Au;
        tb->focused_bg_color = 0xFF101012u;
        tb->text_color = COL_TEXT;
        tb->border_color = 0xFF343437u;
        tb->hover_border_color = 0xFF4A4A50u;
        tb->focused_border_color = 0xFF7B4BEAu;
        tb->cursor_color = COL_TEXT;
        tb->placeholder_color = 0xFF77777Du;
        if (font)
            aroma_textbox_set_font(n, font);
    }
}

static void db_sync_switches(void)
{
    db_switch_sync_guard(true);
    aroma_switch_set_state(db_node("switch_privacy"), s_hide_money);
    aroma_switch_set_state(db_node("switch_pref0"), s_prefs[0]);
    aroma_switch_set_state(db_node("switch_pref1"), s_prefs[1]);
    aroma_switch_set_state(db_node("switch_pref2"), s_prefs[2]);
    db_switch_sync_guard(false);
}

static void db_set_initial_inputs(void)
{
    db_set_textbox("settings_name", s_profile_name);
    db_set_textbox("settings_email", s_profile_email);
    db_set_textbox("search_input", "");
    db_set_textbox("txn_search_input", "");
    db_set_textbox("transfer_recipient", "");
    db_set_textbox("transfer_amount", "");
    db_set_textbox("request_payer", "");
    db_set_textbox("request_amount", "");
}

void loud_dashboard_after_mount(IncenseRegistry *registry)
{
    s_registry = registry;
    db_reset_data(false);
    s_nav = 1;
    s_period = 1;
    db_style_textboxes();
    db_set_initial_inputs();
    db_sync_switches();
    db_reset_heat_colors();
    db_update_profile_labels();
    db_update_prefs_status();
    db_update_privacy_status();
    db_update_notifications();
    db_render_recent();
    db_render_transactions();
    db_render_search();
    db_apply_nav();
    db_apply_period();
    db_render_dashboard();
    db_center_label("signedout_title", 640, 32);
    db_center_label("signedout_note", 640, 14);
    db_hide_overlays();
    aroma_ui_request_redraw(NULL);
}

static void db_reg_void(const char *name, void (*fn)(void *), void *userdata)
{
    IncenseRegisterCallback(name, INCENSE_CALLBACK_VOID_PTR, (void *)fn, userdata);
}

void loud_dashboard_register_callbacks(void)
{
    for (int i = 0; i < LOUD_NAV; i++) {
        char name[32];
        snprintf(name, sizeof(name), "dashboard_nav_%d", i);
        db_reg_void(name, db_nav_cb, (void *)(intptr_t)i);
    }

    db_reg_void("dashboard_period_week", db_period_cb, (void *)(intptr_t)0);
    db_reg_void("dashboard_period_month", db_period_cb, (void *)(intptr_t)1);
    db_reg_void("dashboard_period_year", db_period_cb, (void *)(intptr_t)2);

    db_reg_void("dashboard_search_open", db_search_open_cb, NULL);
    IncenseRegisterCallback("dashboard_search_change",
                            INCENSE_CALLBACK_NODE_STRING_PTR,
                            (void *)db_search_change_cb, NULL);
    for (int i = 0; i < LOUD_SEARCH_ROWS; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_search_hit%d", i);
        db_reg_void(name, db_search_hit_cb, (void *)(intptr_t)i);
    }

    db_reg_void("dashboard_notifications", db_notifications_cb, NULL);
    db_reg_void("dashboard_notifications_read_all", db_notifications_read_all_cb, NULL);
    for (int i = 0; i < LOUD_NOTIF_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof(name), "dashboard_notif_%d", i);
        db_reg_void(name, db_notif_cb, (void *)(intptr_t)i);
    }

    db_reg_void("dashboard_profile", db_profile_cb, NULL);
    db_reg_void("dashboard_profile_settings", db_profile_settings_cb, NULL);
    db_reg_void("dashboard_profile_billing", db_profile_billing_cb, NULL);
    db_reg_void("dashboard_profile_signout", db_profile_signout_cb, NULL);

    db_reg_void("dashboard_quick_menu", db_quick_menu_cb, NULL);
    db_reg_void("dashboard_chart_menu", db_chart_menu_cb, NULL);
    db_reg_void("dashboard_close_overlay", db_close_overlay_cb, NULL);
    db_reg_void("dashboard_noop", db_noop_cb, NULL);

    db_reg_void("dashboard_goto_analytics", db_goto_analytics_cb, NULL);
    db_reg_void("dashboard_goto_transactions", db_goto_transactions_cb, NULL);
    db_reg_void("dashboard_goto_reports", db_goto_reports_cb, NULL);
    db_reg_void("dashboard_reset_confirm", db_reset_confirm_cb, NULL);
    db_reg_void("dashboard_signout_confirm", db_signout_confirm_cb, NULL);
    db_reg_void("dashboard_confirm_yes", db_confirm_yes_cb, NULL);
    db_reg_void("dashboard_sign_back_in", db_sign_back_in_cb, NULL);

    db_reg_void("dashboard_settings_save", db_settings_save_cb, NULL);
    IncenseRegisterCallback("dashboard_switch_privacy",
                            INCENSE_CALLBACK_BOOL_PTR,
                            (void *)db_switch_privacy_cb, NULL);
    for (int i = 0; i < 3; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_switch_pref%d", i);
        IncenseRegisterCallback(name, INCENSE_CALLBACK_BOOL_PTR,
                                (void *)db_switch_pref_cb, (void *)(intptr_t)i);
    }

    db_reg_void("dashboard_transfer", db_open_transfer_cb, NULL);
    db_reg_void("dashboard_request", db_open_request_cb, NULL);
    db_reg_void("dashboard_transfer_submit", db_transfer_submit_cb, NULL);
    db_reg_void("dashboard_request_submit", db_request_submit_cb, NULL);
    for (int i = 0; i < 3; i++) {
        char name[48];
        snprintf(name, sizeof(name), "dashboard_transfer_amount_%d", i);
        db_reg_void(name, db_transfer_amount_cb, (void *)(intptr_t)i);
        snprintf(name, sizeof(name), "dashboard_request_amount_%d", i);
        db_reg_void(name, db_request_amount_cb, (void *)(intptr_t)i);
    }

    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "dashboard_txn_%d", i);
        db_reg_void(name, db_txn_recent_cb, (void *)(intptr_t)i);
    }
    for (int i = 0; i < 3; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_dash_txn_%d", i);
        db_reg_void(name, db_dash_txn_cb, (void *)(intptr_t)i);
    }
    for (int i = 0; i < LOUD_TABLE_ROWS; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_txnrow_%d", i);
        db_reg_void(name, db_txn_row_cb, (void *)(intptr_t)i);
    }
    for (int i = 0; i < 4; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_txn_filter_%d", i);
        db_reg_void(name, db_txn_filter_cb, (void *)(intptr_t)i);
    }
    IncenseRegisterCallback("dashboard_txn_search",
                            INCENSE_CALLBACK_NODE_STRING_PTR,
                            (void *)db_txn_search_cb, NULL);

    db_reg_void("dashboard_detail_receipt", db_detail_receipt_cb, NULL);
    db_reg_void("dashboard_detail_review", db_detail_review_cb, NULL);

    for (int i = 0; i < 42; i++) {
        char name[40];
        snprintf(name, sizeof(name), "dashboard_heat_%d", i);
        db_reg_void(name, db_heat_cb, (void *)(intptr_t)i);
    }
    db_reg_void("dashboard_export_heatmap", db_export_heatmap_cb, NULL);
    db_reg_void("dashboard_export_chart", db_export_chart_cb, NULL);
    db_reg_void("dashboard_export_transactions", db_export_transactions_cb, NULL);
    db_reg_void("dashboard_export_statement", db_export_statement_cb, NULL);
    db_reg_void("dashboard_export_account", db_export_account_cb, NULL);
    db_reg_void("dashboard_reset_period", db_reset_period_cb, NULL);

    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "dashboard_report_%d", i);
        db_reg_void(name, db_report_cb, (void *)(intptr_t)i);
    }
}
