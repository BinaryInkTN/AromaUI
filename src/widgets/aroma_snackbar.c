#include "widgets/aroma_snackbar.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_dp.h"
#include <limits.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_SNACKBAR_TEXT_MAX 128

#define SNACKBAR_HEIGHT_DP 48
#define SNACKBAR_MARGIN_DP 16
#define SNACKBAR_PADDING_H_DP 16
#define SNACKBAR_CORNER_RADIUS_DP 4
#define SNACKBAR_MIN_WIDTH_DP 320
#define SNACKBAR_MAX_WIDTH_DP 544
#define SNACKBAR_ACTION_GAP_DP 24

#ifdef __ANDROID__
static inline int sb_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float sb_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int sb_dp(int dp) { return dp; }
static inline float sb_dp_f(float dp) { return dp; }
#endif

typedef struct AromaSnackbar
{
    AromaRect rect;
    void (*action_callback)(void *user_data);
    void *user_data;
    AromaFont *font;
    AromaNode *self_node;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t action_color;
    float corner_radius;
    float text_scale;
    char message[AROMA_SNACKBAR_TEXT_MAX];
    char action_label[32];
    bool visible;
    bool use_theme_colors;
    int active_pointer_id;
    int base_x;
    int base_y;
    bool has_base_position;
    int window_width;
    int window_height;
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

static bool __snackbar_point_in_bounds(AromaSnackbar* bar, int x, int y)
{
    if (!bar) return false;
    return (x >= bar->rect.x && x <= (bar->rect.x + bar->rect.width) &&
            y >= bar->rect.y && y <= (bar->rect.y + bar->rect.height));
}

static void __calculate_snackbar_size(AromaSnackbar *bar, size_t window_id)
{
    if (!bar)
        return;

    int win_w, win_h;
    __snackbar_get_window_size(window_id, &win_w, &win_h);

    bar->window_width = win_w;
    bar->window_height = win_h;

    if (bar->font)
    {
        int msg_width = aroma_font_get_line_width(bar->font, bar->message);
        int action_width = bar->action_label[0]
                               ? aroma_font_get_line_width(bar->font, bar->action_label) + sb_dp(SNACKBAR_ACTION_GAP_DP)
                               : 0;
        bar->rect.width = sb_dp(SNACKBAR_PADDING_H_DP) * 2 + msg_width + action_width;
    }
    else
    {
        bar->rect.width = sb_dp(SNACKBAR_MIN_WIDTH_DP);
    }

    if (bar->rect.width < sb_dp(SNACKBAR_MIN_WIDTH_DP))
        bar->rect.width = sb_dp(SNACKBAR_MIN_WIDTH_DP);
    if (bar->rect.width > sb_dp(SNACKBAR_MAX_WIDTH_DP))
        bar->rect.width = sb_dp(SNACKBAR_MAX_WIDTH_DP);

    bar->rect.height = sb_dp(SNACKBAR_HEIGHT_DP);

    if (!bar->has_base_position)
    {
        bar->base_x = (win_w - bar->rect.width) / 2;
        bar->base_y = win_h - bar->rect.height - sb_dp(SNACKBAR_MARGIN_DP);
        bar->has_base_position = true;
    }

    bar->rect.x = bar->base_x;
    bar->rect.y = bar->base_y;
}

void aroma_snackbar_draw(AromaNode *snackbar_node, size_t window_id)
{
    if (!snackbar_node)
        return;

    if (aroma_node_is_hidden(snackbar_node))
        return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar || !bar->visible)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;


    if (bar->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        bar->action_color = theme.colors.primary;
        if (aroma_theme_is_dark(&theme))
        {


            bar->bg_color = aroma_color_blend(theme.colors.text_primary,
                                              theme.colors.surface, 0.12f);
            bar->text_color = theme.colors.surface;
        }
        else
        {
            bar->bg_color = 0x333333FF;
            bar->text_color = 0xFFFFFFFF;
        }
    }

    gfx->fill_rectangle(window_id,
                        bar->rect.x, bar->rect.y,
                        bar->rect.width, bar->rect.height,
                        bar->bg_color, true, bar->corner_radius);

    if (bar->font && gfx->render_text)
    {
        int line_h = aroma_font_get_line_height(bar->font);
        int text_y = bar->rect.y + (bar->rect.height - line_h) / 2;

        gfx->render_text(window_id, bar->font, bar->message,
                         bar->rect.x + sb_dp(SNACKBAR_PADDING_H_DP), text_y,
                         bar->text_color, bar->text_scale);

        if (bar->action_label[0])
        {
            int action_text_width = aroma_font_get_line_width(bar->font, bar->action_label);
            gfx->render_text(window_id, bar->font, bar->action_label,
                             bar->rect.x + bar->rect.width - action_text_width - sb_dp(SNACKBAR_PADDING_H_DP),
                             text_y,
                             bar->action_color, bar->text_scale);
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

    bar->corner_radius = sb_dp_f((float)SNACKBAR_CORNER_RADIUS_DP);
    bar->text_scale = 1.0f;
    bar->visible = false;
    bar->active_pointer_id = -1;
    bar->has_base_position = false;

    strncpy(bar->message, message, AROMA_SNACKBAR_TEXT_MAX - 1);
    bar->message[AROMA_SNACKBAR_TEXT_MAX - 1] = '\0';

    AromaTheme theme = aroma_theme_get_global();
    bar->bg_color = 0x333333FF;
    bar->text_color = 0xFFFFFFFF;
    bar->action_color = theme.colors.primary;
    bar->use_theme_colors = true;

    __calculate_snackbar_size(bar, 0);

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
    bar->has_base_position = false;
}

void aroma_snackbar_set_action(AromaNode *snackbar_node, const char *action_text,
                               void (*callback)(void *user_data), void *user_data)
{
    if (!snackbar_node) return;
    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar) return;

    strncpy(bar->action_label, action_text, sizeof(bar->action_label) - 1);
    bar->action_label[sizeof(bar->action_label) - 1] = '\0';
    bar->action_callback = callback;
    bar->user_data = user_data;
    bar->has_base_position = false;
}
void aroma_snackbar_show(AromaNode *snackbar_node)
{
    if (!snackbar_node)
        return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (!bar)
        return;

    bar->visible = true;
    bar->has_base_position = false;
    if (bar->self_node)
    {
        aroma_node_set_hidden(bar->self_node, false);
        aroma_node_invalidate(bar->self_node);
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

    bar->visible = false;

    if (bar->self_node)
    {
        aroma_node_set_hidden(bar->self_node, true);
        aroma_node_invalidate(bar->self_node);
    }

    aroma_ui_request_redraw(NULL);
}

void aroma_snackbar_destroy(AromaNode *snackbar_node)
{
    if (!snackbar_node) return;

    AromaSnackbar *bar = (AromaSnackbar *)snackbar_node->node_widget_ptr;
    if (bar)
    {
        aroma_widget_free(bar);

        snackbar_node->node_widget_ptr = NULL;
    }
    __destroy_node(snackbar_node);
}

static bool __snackbar_default_mouse_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    AromaSnackbar* bar = (AromaSnackbar *)event->target_node->node_widget_ptr;
    if (!bar || !bar->visible) return false;

    int x = 0, y = 0;
    bool handle_logic = false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_RELEASE:
            if (bar->active_pointer_id != -1) {
                bar->active_pointer_id = -1;
                handle_logic = true;
                x = event->data.mouse.x;
                y = event->data.mouse.y;
            }
            break;
        case EVENT_TYPE_MOUSE_CLICK:
            if (bar->active_pointer_id == -1) {
                bar->active_pointer_id = 0;
                handle_logic = true;
                x = event->data.mouse.x;
                y = event->data.mouse.y;
            }
            break;
        case EVENT_TYPE_TOUCH_UP:
            if (bar->active_pointer_id == event->data.touch.id) {
                bar->active_pointer_id = -1;
                handle_logic = true;
                x = event->data.touch.x;
                y = event->data.touch.y;
            }
            break;
        case EVENT_TYPE_TOUCH_DOWN:
            if (bar->active_pointer_id == -1) {
                bar->active_pointer_id = event->data.touch.id;
                handle_logic = true;
                x = event->data.touch.x;
                y = event->data.touch.y;
            }
            break;
        default:
            break;
    }

    if (!handle_logic) return false;

    if (!__snackbar_point_in_bounds(bar, x, y))
        return false;

    if (bar->action_label[0] && bar->action_callback)
    {
        int action_width = aroma_font_get_line_width(bar->font, bar->action_label) + sb_dp(SNACKBAR_ACTION_GAP_DP);
        if (x >= bar->rect.x + bar->rect.width - action_width)
        {
            bar->action_callback(bar->user_data);
            if (event->event_type == EVENT_TYPE_MOUSE_CLICK || event->event_type == EVENT_TYPE_TOUCH_DOWN) {
                if (bar->self_node) {
                    aroma_node_set_hidden(bar->self_node, true);
                    aroma_node_invalidate(bar->self_node);
                }
            }
            return true;
        }
    }

    return false;
}

bool aroma_snackbar_setup_events(AromaNode* snackbar_node, void (*on_redraw_callback)(void*), void* user_data)
{
    if (!snackbar_node) return false;

    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_MOUSE_CLICK, __snackbar_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __snackbar_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_TOUCH_DOWN, __snackbar_default_mouse_handler, (void*)on_redraw_callback, 90);
    aroma_event_subscribe(snackbar_node->node_id, EVENT_TYPE_TOUCH_UP, __snackbar_default_mouse_handler, (void*)on_redraw_callback, 90);

    return true;
}