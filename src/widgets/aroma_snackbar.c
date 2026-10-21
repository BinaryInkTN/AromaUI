#include "widgets/aroma_snackbar.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "core/aroma_timer.h"
#include "aroma_animation.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_dp.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_SNACKBAR_TEXT_MAX 128
#define AROMA_SNACKBAR_ACTION_MAX 32

#define SNACKBAR_MIN_HEIGHT_DP 48
#define SNACKBAR_V_PADDING_DP 12
#define SNACKBAR_MARGIN_DP 16
#define SNACKBAR_PADDING_H_DP 16
#define SNACKBAR_DEFAULT_RADIUS_DP 4
#define SNACKBAR_MIN_WIDTH_DP 320
#define SNACKBAR_MAX_WIDTH_DP 544
#define SNACKBAR_ACTION_GAP_DP 24
#define SNACKBAR_MIN_TEXT_WIDTH_DP 48

#define SNACKBAR_SHOW_FALLBACK_MS 180
#define SNACKBAR_HIDE_FALLBACK_MS 140

#ifdef __ANDROID__
static inline int sb_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float sb_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int sb_dp(int dp) { return dp; }
static inline float sb_dp_f(float dp) { return dp; }
#endif

typedef struct AromaSnackbar
{
    /* MUST stay first: the node's widget pointer doubles as the node rect
       (aroma_node_get_rect casts node_widget_ptr to AromaRect*), which is
       what hit testing uses. */
    AromaRect rect;

    void (*action_callback)(void *user_data);
    void *user_data;
    AromaFont *font;
    AromaNode *self_node;

    char message[AROMA_SNACKBAR_TEXT_MAX];
    char fit_message[AROMA_SNACKBAR_TEXT_MAX];
    char action_label[AROMA_SNACKBAR_ACTION_MAX];
    char fit_action[AROMA_SNACKBAR_ACTION_MAX];

    /* Theme-derived unless overridden through aroma_snackbar_set_colors. */
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t action_color;
    bool custom_bg;
    bool custom_text;

    float corner_radius; /* < 0 = follow the theme */
    float text_scale;

    bool visible;        /* on screen (including show/hide animation) */
    bool hiding;         /* exit animation in flight */
    bool events_bound;
    float anim_progress; /* 0 = off screen, 1 = fully shown (eased) */

    int duration_ms;
    AromaTimer *dismiss_timer;
    int active_pointer_id;
    int pressed_arm;     /* 0 = none, 1 = action, 2 = plain body */
    int action_hit_x0;   /* left edge of the action hit region */
} AromaSnackbar;

static void __snackbar_get_window_size(size_t window_id, int *w, int *h)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
    {
        platform->get_window_size(window_id, w, h);
    }
    else
    {
        *w = 800;
        *h = 600;
    }
}

static bool __snackbar_point_in_bounds(AromaSnackbar *bar, int x, int y)
{
    if (!bar) return false;
    return (x >= bar->rect.x && x <= (bar->rect.x + bar->rect.width) &&
            y >= bar->rect.y && y <= (bar->rect.y + bar->rect.height));
}

static int sb_text_width(AromaFont *font, const char *text, float scale)
{
    if (!font || !text) return 0;
    if (scale <= 0.0f) scale = 1.0f;
    float w = (float)aroma_font_get_line_width(font, text) * scale;
    return (int)(w + 0.5f);
}

/* Copies `src` into `dst`, truncating on a UTF-8 boundary with an ellipsis so
   the result measures at most `max_width` pixels. Returns the drawn width. */
static int sb_fit_text(AromaFont *font, const char *src, char *dst,
                       size_t dst_size, float scale, int max_width)
{
    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;

    if (!font || max_width <= 0)
    {
        /* No way to measure: keep a conservative prefix (byte-safe). */
        size_t n = strlen(src);
        if (n > dst_size - 1) n = dst_size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
        memcpy(dst, src, n);
        dst[n] = '\0';
        return 0;
    }

    int full_w = sb_text_width(font, src, scale);
    if (full_w <= max_width)
    {
        snprintf(dst, dst_size, "%s", src);
        return full_w;
    }

    const char *ellipsis = "\xE2\x80\xA6"; /* … */
    int ellipsis_w = sb_text_width(font, ellipsis, scale);
    if (ellipsis_w <= 0)
    {
        ellipsis = "...";
        ellipsis_w = sb_text_width(font, ellipsis, scale);
    }

    size_t len = strlen(src);
    size_t ends[AROMA_SNACKBAR_TEXT_MAX];
    size_t count = 0;
    for (size_t i = 0; i < len && count < AROMA_SNACKBAR_TEXT_MAX;)
    {
        unsigned char c = (unsigned char)src[i];
        size_t step = 1;
        if (c >= 0xF0) step = 4;
        else if (c >= 0xE0) step = 3;
        else if (c >= 0xC0) step = 2;
        if (i + step > len) step = len - i;
        i += step;
        ends[count++] = i;
    }

    size_t keep = 0;
    int keep_w = 0;
    for (size_t k = count; k > 0; k--)
    {
        size_t n = ends[k - 1];
        char tmp[AROMA_SNACKBAR_TEXT_MAX];
        memcpy(tmp, src, n);
        tmp[n] = '\0';
        int w = sb_text_width(font, tmp, scale);
        if (w + ellipsis_w <= max_width)
        {
            keep = n;
            keep_w = w;
            break;
        }
    }

    size_t ellipsis_len = strlen(ellipsis);
    if (keep + ellipsis_len + 1 > dst_size)
    {
        keep = (dst_size > ellipsis_len + 1) ? dst_size - ellipsis_len - 1 : 0;
        while (keep > 0 && ((unsigned char)src[keep] & 0xC0) == 0x80) keep--;
    }
    memcpy(dst, src, keep);
    memcpy(dst + keep, ellipsis, ellipsis_len);
    dst[keep + ellipsis_len] = '\0';
    return keep_w + ellipsis_w;
}

static float sb_luma(uint32_t color)
{
    uint8_t r, g, b;
    aroma_color_extract_rgb(color, &r, &g, &b);
    return (0.2126f * (float)r + 0.7152f * (float)g + 0.0722f * (float)b) / 255.0f;
}

static float sb_contrast(uint32_t a, uint32_t b)
{
    float la = sb_luma(a);
    float lb = sb_luma(b);
    float hi = la > lb ? la : lb;
    float lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

/* Picks the theme accent with the most contrast against the toast body,
   falling back to the body text colour when every accent is too close. */
static uint32_t sb_pick_accent(const AromaTheme *theme, uint32_t bg, uint32_t on_surface)
{
    uint32_t primary = theme->colors.primary ? theme->colors.primary : 0xFF6750A4u;
    uint32_t candidates[4] = {primary, theme->colors.primary_light,
                              theme->colors.primary_dark, theme->colors.secondary};

    uint32_t best = primary;
    float best_ratio = sb_contrast(primary, bg);
    for (int i = 1; i < 4; i++)
    {
        if (!candidates[i]) continue;
        float ratio = sb_contrast(candidates[i], bg);
        if (ratio > best_ratio)
        {
            best = candidates[i];
            best_ratio = ratio;
        }
    }
    if (best_ratio < 3.0f)
        best = on_surface;
    return best;
}

static void __snackbar_resolve_colors(AromaSnackbar *bar)
{
    AromaTheme theme = aroma_theme_get_global();
    uint32_t surface = theme.colors.surface ? theme.colors.surface : 0xFF1A1A1Cu;
    uint32_t on_surface = theme.colors.text_primary ? theme.colors.text_primary : 0xFFFFFFFFu;

    if (!bar->custom_bg)
    {
        /* Inverse surface: the toast flips relative to the app surface, so a
           dark theme gets a light toast and a light theme a dark one. */
        bar->bg_color = aroma_color_blend(surface, on_surface, 0.92f);
    }
    if (!bar->custom_text)
    {
        bar->text_color = aroma_color_blend(on_surface, surface, 0.92f);
    }
    bar->action_color = sb_pick_accent(&theme, bar->bg_color, bar->text_color);
}

static void __snackbar_compute_layout(AromaSnackbar *bar, size_t window_id)
{
    int win_w = 800, win_h = 600;
    __snackbar_get_window_size(window_id, &win_w, &win_h);

    const int pad = sb_dp(SNACKBAR_PADDING_H_DP);
    const int margin = sb_dp(SNACKBAR_MARGIN_DP);

    int avail_w = win_w - 2 * margin;
    if (avail_w < 1) avail_w = 1;

    int max_w = sb_dp(SNACKBAR_MAX_WIDTH_DP);
    if (max_w > avail_w) max_w = avail_w;
    if (max_w < 1) max_w = 1;

    int min_w = sb_dp(SNACKBAR_MIN_WIDTH_DP);
    if (min_w > max_w) min_w = max_w;

    int line_h = bar->font ? aroma_font_get_line_height(bar->font) : 0;
    int height = sb_dp(SNACKBAR_MIN_HEIGHT_DP);
    int need_h = line_h + 2 * sb_dp(SNACKBAR_V_PADDING_DP);
    if (need_h > height) height = need_h;

    /* Action block: capped at a third of the bar so it can never crowd the
       message out; dropped entirely on very narrow windows. */
    int action_w = 0;
    bar->fit_action[0] = '\0';
    if (bar->action_label[0])
    {
        int action_limit = max_w / 3;
        if (action_limit < sb_dp(SNACKBAR_MIN_TEXT_WIDTH_DP))
            action_limit = max_w - 2 * pad;
        if (action_limit > 0)
        {
            int aw = sb_fit_text(bar->font, bar->action_label, bar->fit_action,
                                 sizeof(bar->fit_action), bar->text_scale, action_limit);
            if (bar->fit_action[0])
                action_w = aw + sb_dp(SNACKBAR_ACTION_GAP_DP);
        }
    }

    int text_avail = max_w - 2 * pad - action_w;
    if (text_avail < sb_dp(SNACKBAR_MIN_TEXT_WIDTH_DP))
    {
        action_w = 0;
        bar->fit_action[0] = '\0';
        text_avail = max_w - 2 * pad;
    }
    if (text_avail < 1) text_avail = 1;

    int msg_w = sb_fit_text(bar->font, bar->message, bar->fit_message,
                            sizeof(bar->fit_message), bar->text_scale, text_avail);

    int width = 2 * pad + msg_w + action_w;
    if (width < min_w) width = min_w;
    if (width > max_w) width = max_w;
    if (width < 1) width = 1;

    int x = (win_w - width) / 2;
    if (x < 0) x = 0;
    int target_y = win_h - height - margin;
    if (target_y < 0) target_y = 0;

    /* Slide in from below the bottom edge using the eased animation value. */
    float p = bar->anim_progress;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    int slide = (int)((1.0f - p) * (float)(height + margin) + 0.5f);

    bar->rect.x = x;
    bar->rect.y = target_y + slide;
    bar->rect.width = width;
    bar->rect.height = height;

    int action_text_w = action_w > 0 ? action_w - sb_dp(SNACKBAR_ACTION_GAP_DP) : 0;
    bar->action_hit_x0 = bar->rect.x + width - pad - action_text_w;
}

/* The GL backend treats an alpha byte of 0 as "opaque", so translucent values
   must stay >= 1. */
static uint32_t sb_apply_alpha(uint32_t color, float p)
{
    uint8_t r, g, b;
    aroma_color_extract_rgb(color, &r, &g, &b);
    int a = (int)(p * 255.0f + 0.5f);
    if (a < 1) a = 1;
    if (a > 255) a = 255;
    return aroma_color_rgba(r, g, b, (uint8_t)a);
}

/* Text rendering ignores the colour alpha byte, so foreground colours fade by
   blending into the toast body instead. */
static uint32_t sb_apply_fg(float p, uint32_t fg, uint32_t bg)
{
    if (p >= 0.999f) return fg;
    if (p <= 0.001f) return bg;
    return aroma_color_blend(bg, fg, p);
}

static uint32_t sb_anim_duration(int theme_ms, uint32_t fallback)
{
    int ms = theme_ms > 0 ? theme_ms : (int)fallback;
    if (ms < 80) ms = 80;
    if (ms > 400) ms = 400;
    return (uint32_t)ms;
}

static void __snackbar_anim_step(AromaNode *target, float value, void *user_data)
{
    (void)target;
    AromaSnackbar *bar = (AromaSnackbar *)user_data;
    if (bar)
        bar->anim_progress = value;
}

static void __snackbar_finish_hide(AromaNode *target, void *user_data)
{
    (void)target;
    AromaSnackbar *bar = (AromaSnackbar *)user_data;
    if (!bar)
        return;

    bar->anim_progress = 0.0f;
    bar->visible = false;
    bar->hiding = false;
    bar->active_pointer_id = -1;
    bar->pressed_arm = 0;

    if (bar->self_node)
    {
        aroma_node_set_hidden(bar->self_node, true);
        aroma_node_invalidate(bar->self_node);
    }
    aroma_ui_request_redraw(NULL);
}

static void __snackbar_begin_show(AromaSnackbar *bar)
{
    if (!bar)
        return;

    bar->hiding = false;

    if (!bar->self_node)
    {
        bar->anim_progress = 1.0f;
        return;
    }

    if (bar->anim_progress >= 0.999f)
    {
        /* Already on screen: stop any exit animation so it cannot drag the
           toast back down, then settle at full opacity. */
        aroma_animation_stop(bar->self_node);
        bar->anim_progress = 1.0f;
        return;
    }

    AromaTheme theme = aroma_theme_get_global();
    uint32_t dur = sb_anim_duration(theme.transition_duration_ms, SNACKBAR_SHOW_FALLBACK_MS);
    AromaAnimation *a = aroma_animation_start_custom(bar->self_node,
                                                     bar->anim_progress, 1.0f,
                                                     dur, __snackbar_anim_step, bar);
    if (a)
        aroma_animation_set_easing(a, AROMA_EASE_OUT_CUBIC);
    else
        bar->anim_progress = 1.0f;
}

static void __snackbar_begin_hide(AromaSnackbar *bar)
{
    if (!bar)
        return;

    if (bar->dismiss_timer)
    {
        aroma_timer_cancel(bar->dismiss_timer);
        bar->dismiss_timer = NULL;
    }

    if (!bar->visible && !bar->hiding)
        return;

    bar->hiding = true;
    bar->pressed_arm = 0;

    AromaTheme theme = aroma_theme_get_global();
    uint32_t dur = sb_anim_duration(theme.transition_duration_ms, SNACKBAR_HIDE_FALLBACK_MS);

    if (!bar->self_node || bar->anim_progress <= 0.001f)
    {
        __snackbar_finish_hide(NULL, bar);
        return;
    }

    AromaAnimation *a = aroma_animation_start_custom(bar->self_node,
                                                     bar->anim_progress, 0.0f,
                                                     dur, __snackbar_anim_step, bar);
    if (a)
    {
        aroma_animation_set_easing(a, AROMA_EASE_IN_OUT_QUAD);
        aroma_animation_set_on_complete(a, __snackbar_finish_hide);
    }
    else
    {
        __snackbar_finish_hide(NULL, bar);
    }
}

void aroma_snackbar_draw(AromaNode *snackbar_node, size_t window_id)
{
    if (!snackbar_node || aroma_node_is_hidden(snackbar_node))
        return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar || !bar->visible)
        return;

    float p = bar->anim_progress;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (p <= 0.001f)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle)
        return;

    __snackbar_resolve_colors(bar);
    __snackbar_compute_layout(bar, window_id);

    AromaTheme theme = aroma_theme_get_global();

    float radius = bar->corner_radius;
    if (radius < 0.0f)
    {
        radius = (float)theme.spacing.border_radius;
        if (radius <= 0.0f)
            radius = sb_dp_f(SNACKBAR_DEFAULT_RADIUS_DP);
        float max_r = (float)bar->rect.height / 2.0f;
        if (radius > max_r)
            radius = max_r;
    }

    if (theme.enable_shadows)
    {
        int spread = sb_dp(3);
        int offset = sb_dp(2);
        uint32_t soft = sb_apply_alpha(0xFF000000u, p * 0.10f);
        uint32_t near = sb_apply_alpha(0xFF000000u, p * 0.14f);
        gfx->fill_rectangle(window_id,
                            bar->rect.x - spread, bar->rect.y - spread + offset,
                            bar->rect.width + 2 * spread, bar->rect.height + 2 * spread,
                            soft, true, radius + (float)spread);
        gfx->fill_rectangle(window_id,
                            bar->rect.x - spread / 2, bar->rect.y - spread / 2 + offset,
                            bar->rect.width + spread, bar->rect.height + spread,
                            near, true, radius + (float)(spread / 2));
    }

    gfx->fill_rectangle(window_id,
                        bar->rect.x, bar->rect.y,
                        bar->rect.width, bar->rect.height,
                        sb_apply_alpha(bar->bg_color, p), true, radius);

    if (bar->font && gfx->render_text && bar->fit_message[0])
    {
        int pad = sb_dp(SNACKBAR_PADDING_H_DP);
        int line_h = (int)((float)aroma_font_get_line_height(bar->font) * bar->text_scale);
        int text_y = bar->rect.y + (bar->rect.height - line_h) / 2;

        gfx->render_text(window_id, bar->font, bar->fit_message,
                         bar->rect.x + pad, text_y,
                         sb_apply_fg(p, bar->text_color, bar->bg_color),
                         bar->text_scale);

        if (bar->fit_action[0])
        {
            int action_w = sb_text_width(bar->font, bar->fit_action, bar->text_scale);
            gfx->render_text(window_id, bar->font, bar->fit_action,
                             bar->rect.x + bar->rect.width - pad - action_w,
                             text_y,
                             sb_apply_fg(p, bar->action_color, bar->bg_color),
                             bar->text_scale);
        }
    }
}

AromaNode *aroma_snackbar_create(AromaNode *parent, const char *message, int duration_ms)
{
    if (!parent || !message)
        return NULL;

    AromaSnackbar *bar = (AromaSnackbar *)aroma_widget_alloc(sizeof(AromaSnackbar));
    if (!bar)
        return NULL;
    memset(bar, 0, sizeof(AromaSnackbar));

    bar->corner_radius = -1.0f; /* follow the theme */
    bar->text_scale = 1.0f;
    bar->visible = false;
    bar->hiding = false;
    bar->duration_ms = duration_ms > 0 ? duration_ms : 4000;
    bar->active_pointer_id = -1;
    bar->pressed_arm = 0;
    bar->anim_progress = 0.0f;

    snprintf(bar->message, sizeof(bar->message), "%s", message);

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, bar);
    if (!node)
    {
        aroma_widget_free(bar);
        return NULL;
    }

    bar->self_node = node;
    aroma_node_set_z_index(node, INT_MAX);
    aroma_node_set_draw_cb(node, aroma_snackbar_draw);
    aroma_node_set_hidden(node, true);

    return node;
}

void aroma_snackbar_set_font(AromaNode *snackbar_node, AromaFont *font)
{
    if (!snackbar_node) return;
    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar) return;

    bar->font = font;
    aroma_node_invalidate(snackbar_node);
}

void aroma_snackbar_set_action(AromaNode *snackbar_node, const char *action_text,
                               void (*callback)(void *user_data), void *user_data)
{
    if (!snackbar_node) return;
    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar) return;

    if (action_text)
        snprintf(bar->action_label, sizeof(bar->action_label), "%s", action_text);
    else
        bar->action_label[0] = '\0';

    bar->action_callback = callback;
    bar->user_data = user_data;
    aroma_node_invalidate(snackbar_node);
}

void aroma_snackbar_set_colors(AromaNode *snackbar_node, uint32_t bg_color, uint32_t text_color)
{
    if (!snackbar_node) return;
    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar) return;

    /* 0 keeps the theme-derived colour for that slot. */
    if (bg_color != 0)
    {
        bar->bg_color = bg_color;
        bar->custom_bg = true;
    }
    if (text_color != 0)
    {
        bar->text_color = text_color;
        bar->custom_text = true;
    }
    aroma_node_invalidate(snackbar_node);
}

void aroma_snackbar_set_corner_radius(AromaNode *snackbar_node, float radius)
{
    if (!snackbar_node) return;
    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar) return;

    bar->corner_radius = radius >= 0.0f ? radius : -1.0f;
    aroma_node_invalidate(snackbar_node);
}

static void __snackbar_timeout_cb(void *user_data)
{
    AromaSnackbar *bar = (AromaSnackbar *)user_data;
    if (!bar)
        return;

    bar->dismiss_timer = NULL;
    __snackbar_begin_hide(bar);
}

void aroma_snackbar_show(AromaNode *snackbar_node)
{
    if (!snackbar_node)
        return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar)
        return;

    if (bar->dismiss_timer)
    {
        aroma_timer_cancel(bar->dismiss_timer);
        bar->dismiss_timer = NULL;
    }

    bar->visible = true;
    bar->active_pointer_id = -1;
    bar->pressed_arm = 0;

    if (bar->self_node)
    {
        aroma_node_set_hidden(bar->self_node, false);
        aroma_node_invalidate(bar->self_node);
    }

    __snackbar_begin_show(bar);

    if (bar->duration_ms > 0)
    {
        bar->dismiss_timer = aroma_timer_create((uint32_t)bar->duration_ms, false,
                                                __snackbar_timeout_cb, bar);
    }

    aroma_ui_request_redraw(NULL);
}

void aroma_snackbar_dismiss(AromaNode *snackbar_node)
{
    if (!snackbar_node)
        return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar)
        return;

    __snackbar_begin_hide(bar);
}

void aroma_snackbar_destroy(AromaNode *snackbar_node)
{
    if (!snackbar_node) return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (bar)
    {
        if (bar->dismiss_timer)
        {
            aroma_timer_cancel(bar->dismiss_timer);
            bar->dismiss_timer = NULL;
        }
        aroma_animation_stop(snackbar_node);
        aroma_widget_free(bar);

        snackbar_node->node_widget_ptr = NULL;
    }
    __destroy_node(snackbar_node);
}

static bool __snackbar_action_hit(AromaSnackbar *bar, int x)
{
    if (!bar->fit_action[0] || !bar->action_callback)
        return false;
    return x >= bar->action_hit_x0;
}

static bool __snackbar_pointer_down(AromaSnackbar *bar, int pointer_id, int x, int y)
{
    if (!bar || !bar->self_node)
        return false;
    if (!__snackbar_point_in_bounds(bar, x, y))
        return false;

    /* A missed release leaves the id latched; reset it rather than swallow
       every following press. */
    if (bar->active_pointer_id != -1 && bar->active_pointer_id != pointer_id)
        bar->active_pointer_id = -1;
    if (bar->active_pointer_id != -1)
        return false;

    bar->active_pointer_id = pointer_id;

    if (__snackbar_action_hit(bar, x))
        bar->pressed_arm = 1;
    else if (!bar->fit_action[0])
        bar->pressed_arm = 2; /* plain toast: tapping the body dismisses */
    else
        bar->pressed_arm = 0; /* has an action: stray taps do nothing */
    return true;
}

static bool __snackbar_pointer_up(AromaSnackbar *bar, int pointer_id, int x, int y)
{
    if (!bar || bar->active_pointer_id != pointer_id)
        return false;

    bar->active_pointer_id = -1;
    int arm = bar->pressed_arm;
    bar->pressed_arm = 0;

    if (!__snackbar_point_in_bounds(bar, x, y))
        return false;

    if (arm == 1 && __snackbar_action_hit(bar, x))
    {
        if (bar->action_callback)
            bar->action_callback(bar->user_data);
        aroma_snackbar_dismiss(bar->self_node);
        return true;
    }
    if (arm == 2)
    {
        aroma_snackbar_dismiss(bar->self_node);
        return true;
    }
    return false;
}

static bool __snackbar_default_mouse_handler(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || !event->target_node) return false;

    AromaSnackbar *bar = (AromaSnackbar *)event->target_node->node_widget_ptr;
    if (!bar || !bar->visible || bar->hiding) return false;

    /* Ignore taps while the toast is still sliding in. */
    if (bar->anim_progress < 0.4f) return false;

    switch (event->event_type)
    {
        case EVENT_TYPE_MOUSE_CLICK:
            return __snackbar_pointer_down(bar, 0, event->data.mouse.x, event->data.mouse.y);
        case EVENT_TYPE_MOUSE_RELEASE:
            return __snackbar_pointer_up(bar, 0, event->data.mouse.x, event->data.mouse.y);
        case EVENT_TYPE_TOUCH_DOWN:
            return __snackbar_pointer_down(bar, event->data.touch.id,
                                           event->data.touch.x, event->data.touch.y);
        case EVENT_TYPE_TOUCH_UP:
            return __snackbar_pointer_up(bar, event->data.touch.id,
                                         event->data.touch.x, event->data.touch.y);
        default:
            return false;
    }
}

bool aroma_snackbar_setup_events(AromaNode *snackbar_node, void (*on_redraw_callback)(void *), void *user_data)
{
    if (!snackbar_node) return false;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (bar && bar->events_bound)
        return true;
    if (bar)
        bar->events_bound = true;

    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_MOUSE_CLICK, __snackbar_default_mouse_handler, (void *)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __snackbar_default_mouse_handler, (void *)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_TOUCH_DOWN, __snackbar_default_mouse_handler, (void *)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_TOUCH_UP, __snackbar_default_mouse_handler, (void *)on_redraw_callback, 90);

    return true;
}
