#include "widgets/aroma_dialog.h"
#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include "aroma_dp.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_DIALOG_ACTION_MAX 3
#define AROMA_DIALOG_PADDING_DP 16
#define AROMA_DIALOG_BUTTON_HEIGHT_DP 36
#define AROMA_DIALOG_BUTTON_EXTRA_DP 24
#define AROMA_DIALOG_SPACING_DP 8
#define AROMA_DIALOG_TEXT_W_DP 48
#define AROMA_DIALOG_BUTTON_MIN_W_DP 64
#define AROMA_DIALOG_CONTENT_Y_DP 72
#define AROMA_DIALOG_CORNER_RADIUS_DP 12
#define AROMA_DIALOG_BUTTON_RADIUS_DP 8
#define AROMA_DIALOG_TITLE_X_DP 16
#define AROMA_DIALOG_TITLE_Y_DP 22
#define AROMA_DIALOG_GAP_SMALL_DP 10
#define AROMA_DIALOG_GAP_MED_DP 12
#define AROMA_DIALOG_MESSAGE_W_DP 32
#define AROMA_DIALOG_FONT_H_DP 20
#define AROMA_DIALOG_SCREEN_MARGIN_DP 24

#ifdef __ANDROID__
static inline int dialog_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float dialog_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int dialog_dp(int dp) { return dp; }
static inline float dialog_dp_f(float dp) { return dp; }
#endif

typedef struct
{
    char label[32];
    void (*callback)(void *user_data);
    void *user_data;
} AromaDialogAction;

typedef struct AromaDialog
{
    AromaRect rect;
    AromaNode *parent_node;
    int width_dp;
    int height_dp;
    int centered_x;
    int centered_y;

    int action_button_widths[AROMA_DIALOG_ACTION_MAX];
    int action_button_x[AROMA_DIALOG_ACTION_MAX];
    int action_button_text_widths[AROMA_DIALOG_ACTION_MAX];
    int action_button_y;
    int action_button_height;
    int action_button_spacing;

    char title[64];
    char message[1024];
    AromaDialogType type;
    bool visible;

    AromaDialogAction actions[AROMA_DIALOG_ACTION_MAX];
    size_t action_count;

    AromaFont *font;

    AromaNode *content_node;
    int content_y_offset;
} AromaDialog;

static void __dialog_request_redraw(void *user_data)
{
    (void)user_data;
    aroma_ui_request_redraw(NULL);
}

static void __dialog_update_rect(AromaDialog *dlg)
{
    if (!dlg)
        return;

    int width = dialog_dp(dlg->width_dp);
    int height = dialog_dp(dlg->height_dp);
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;

    int win_w = 0;
    int win_h = 0;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
        platform->get_window_size(0, &win_w, &win_h);

    if (win_w > 0 && win_h > 0)
    {



        int max_w = win_w - 2 * dialog_dp(AROMA_DIALOG_SCREEN_MARGIN_DP);
        int max_h = win_h - 2 * dialog_dp(AROMA_DIALOG_SCREEN_MARGIN_DP);
        if (max_w < 1)
            max_w = 1;
        if (max_h < 1)
            max_h = 1;
        if (width > max_w)
            width = max_w;
        if (height > max_h)
            height = max_h;
        dlg->centered_x = (win_w - width) / 2;
        dlg->centered_y = (win_h - height) / 2;
    }
    else if (dlg->parent_node)
    {
        AromaRect *pr = aroma_node_get_rect(dlg->parent_node);
        if (pr && pr->width > 0 && pr->height > 0)
        {
            dlg->centered_x = pr->x + (pr->width - width) / 2;
            dlg->centered_y = pr->y + (pr->height - height) / 2;
        }
        else
        {
            dlg->centered_x = 0;
            dlg->centered_y = 0;
        }
    }
    else
    {
        dlg->centered_x = 0;
        dlg->centered_y = 0;
    }

    dlg->rect.x = dlg->centered_x;
    dlg->rect.y = dlg->centered_y;
    dlg->rect.width = width;
    dlg->rect.height = height;
}

static void __dialog_recompute_action_layout(AromaDialog *dlg, AromaGraphicsInterface *gfx, size_t window_id)
{
    if (!dlg)
        return;

    const int padding = dialog_dp(AROMA_DIALOG_PADDING_DP);
    int button_height = dialog_dp(AROMA_DIALOG_BUTTON_HEIGHT_DP);

    if (dlg->font)
    {
        int font_h = aroma_font_get_line_height(dlg->font);
        if (font_h > dialog_dp(AROMA_DIALOG_FONT_H_DP))
        {
            button_height = font_h + dialog_dp(AROMA_DIALOG_BUTTON_EXTRA_DP);
        }
    }

    const int spacing = dialog_dp(AROMA_DIALOG_SPACING_DP);

    dlg->action_button_height = button_height;
    dlg->action_button_spacing = spacing;

    int total_width = 0;

    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int text_w = dialog_dp(AROMA_DIALOG_TEXT_W_DP);
        if (gfx && gfx->measure_text && dlg->font)
        {
            text_w = (int)gfx->measure_text(window_id, dlg->font, dlg->actions[i].label, 1.0f);
        }

        int button_w = text_w + dialog_dp(AROMA_DIALOG_BUTTON_EXTRA_DP);
        int min_w = dialog_dp(AROMA_DIALOG_BUTTON_MIN_W_DP);
        if (button_w < min_w)
            button_w = min_w;

        dlg->action_button_text_widths[i] = text_w;
        dlg->action_button_widths[i] = button_w;
        total_width += button_w;
        if (i + 1 < dlg->action_count)
            total_width += spacing;
    }

    int right = dlg->rect.x + dlg->rect.width - padding;
    int x = right - total_width;
    for (size_t i = 0; i < dlg->action_count; i++)
    {
        dlg->action_button_x[i] = x;
        x += dlg->action_button_widths[i] + spacing;
    }

    int btn_y = dlg->rect.y + dlg->rect.height - padding - button_height;
    dlg->action_button_y = btn_y;
}

static bool __dialog_handle_event(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;

    AromaDialog *dlg = (AromaDialog *)user_data;
    if (!dlg || !dlg->visible)
        return false;
    if (event->event_type != EVENT_TYPE_MOUSE_RELEASE &&
        event->event_type != EVENT_TYPE_MOUSE_CLICK &&
        event->event_type != EVENT_TYPE_TOUCH_UP &&
        event->event_type != EVENT_TYPE_TOUCH_DOWN)
        return false;

    __dialog_update_rect(dlg);
    int mx = 0;
    int my = 0;
    if (event->event_type == EVENT_TYPE_TOUCH_UP ||
        event->event_type == EVENT_TYPE_TOUCH_DOWN)
    {
        mx = event->data.touch.x;
        my = event->data.touch.y;
    }
    else
    {
        mx = event->data.mouse.x;
        my = event->data.mouse.y;
    }
    AromaNode *cur = event->target_node->parent_node;
    while (cur)
    {
        if (cur->node_type == NODE_TYPE_CONTAINER && aroma_container_is_scrollable(cur))
        {
            int scroll_x = 0;
            int scroll_y = 0;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            mx += scroll_x;
            my += scroll_y;
        }
        cur = cur->parent_node;
    }

    bool inside = (mx >= dlg->rect.x && mx <= dlg->rect.x + dlg->rect.width &&
                   my >= dlg->rect.y && my <= dlg->rect.y + dlg->rect.height);
    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int x = dlg->action_button_x[i];
        int y = dlg->action_button_y;
        int w = dlg->action_button_widths[i];
        int h = dlg->action_button_height;

        if (mx >= x && mx <= x + w && my >= y && my <= y + h)
        {
            if (dlg->actions[i].callback)
                dlg->actions[i].callback(dlg->actions[i].user_data);
            dlg->visible = false;
            aroma_node_invalidate(event->target_node);
            aroma_ui_request_redraw(NULL);
            return true;
        }
    }

    return inside;
}

AromaNode *aroma_dialog_create(AromaNode *parent, const char *title, const char *message, int width, int height, AromaDialogType type)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;
    AromaDialog *dlg = (AromaDialog *)aroma_widget_alloc(sizeof(AromaDialog));
    if (!dlg)
        return NULL;

    memset(dlg, 0, sizeof(AromaDialog));

    dlg->parent_node = parent;
    dlg->width_dp = width;
    dlg->height_dp = height;
    dlg->type = type;
    dlg->visible = false;

    if (title)
        strncpy(dlg->title, title, sizeof(dlg->title) - 1);
    if (title)
        dlg->title[sizeof(dlg->title) - 1] = '\0';
    if (message)
        strncpy(dlg->message, message, sizeof(dlg->message) - 1);
    if (message)
        dlg->message[sizeof(dlg->message) - 1] = '\0';

    __dialog_update_rect(dlg);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);

    AromaNode *node = __add_child_node(NODE_TYPE_CONTAINER, parent, dlg);
    if (!node)
    {
        aroma_widget_free(dlg);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_dialog_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __dialog_handle_event, dlg, 100);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __dialog_handle_event, dlg, 100);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, __dialog_handle_event, dlg, 100);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, __dialog_handle_event, dlg, 100);

    const int padding = dialog_dp(AROMA_DIALOG_PADDING_DP);
    dlg->content_y_offset = dialog_dp(AROMA_DIALOG_CONTENT_Y_DP);
    int content_h = dlg->rect.height - dlg->content_y_offset -
                    padding - dialog_dp(AROMA_DIALOG_BUTTON_HEIGHT_DP) - padding;
    if (content_h < 0)
        content_h = 0;
    AromaNode *content = aroma_container_create(
        node,
        padding,
        dlg->content_y_offset,
        dlg->rect.width - padding * 2,
        content_h);
    dlg->content_node = content;

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_dialog_add_action(AromaNode *dialog_node, const char *label, void (*callback)(void *), void *user_data)
{
    if (!dialog_node || !dialog_node->node_widget_ptr || !label)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (dlg->action_count >= AROMA_DIALOG_ACTION_MAX)
        return;

    AromaDialogAction *act = &dlg->actions[dlg->action_count++];
    strncpy(act->label, label, sizeof(act->label) - 1);
    act->label[sizeof(act->label) - 1] = '\0';
    act->callback = callback;
    act->user_data = user_data;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
}

void aroma_dialog_set_font(AromaNode *dialog_node, AromaFont *font)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->font = font;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
}

void aroma_dialog_set_title(AromaNode *dialog_node, const char *title)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (title)
    {
        strncpy(dlg->title, title, sizeof(dlg->title) - 1);
        dlg->title[sizeof(dlg->title) - 1] = '\0';
    }
    else
    {
        dlg->title[0] = '\0';
    }

    __dialog_update_rect(dlg);
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_set_message(AromaNode *dialog_node, const char *message)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (message)
    {
        strncpy(dlg->message, message, sizeof(dlg->message) - 1);
        dlg->message[sizeof(dlg->message) - 1] = '\0';
    }
    else
    {
        dlg->message[0] = '\0';
    }

    __dialog_update_rect(dlg);
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_show(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->visible = true;
    __dialog_update_rect(dlg);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);

    if (dlg->content_node)
        aroma_node_set_hidden(dlg->content_node, false);

    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_hide(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->visible = false;
    if (dlg->content_node)
        aroma_node_set_hidden(dlg->content_node, true);
    aroma_node_set_hidden(dialog_node, true);
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

AromaNode *aroma_dialog_get_content_area(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return NULL;
    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    return dlg->content_node;
}





static size_t dialog_utf8_step(const char *s)
{
    unsigned char c = (unsigned char)*s;
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

static float dialog_measure(AromaGraphicsInterface *gfx, size_t window_id,
                            AromaFont *font, const char *text, size_t len)
{
    char buf[1024];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
    return gfx->measure_text(window_id, font, buf, 1.0f);
}

static void dialog_draw_wrapped(AromaGraphicsInterface *gfx, size_t window_id,
                                AromaFont *font, const char *text, int x,
                                int y_top, int max_w, int line_h,
                                int max_lines, uint32_t color)
{
    if (!text || !text[0] || max_lines < 1 || max_w < 16 || line_h < 1)
        return;
    int line_no = 0;
    const char *p = text;
    char line[1024];
    size_t line_len = 0;



    bool truncated = false;
    while (*p && line_no < max_lines)
    {


        if (*p == '\n')
        {
            p++;
            if (line_len > 0)
            {
                if (line_no + 1 >= max_lines)
                {
                    truncated = true;
                    break;
                }
                line[line_len] = '\0';
                gfx->render_text(window_id, font, line, x,
                                 y_top + line_no * line_h, color, 1.0f);
                line_no++;
                line_len = 0;
            }
            else
            {
                if (line_no + 1 >= max_lines)
                {
                    if (*p)
                        truncated = true;
                    break;
                }
                line_no++;
            }
            continue;
        }

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '\n')
            continue;
        const char *w = p;
        size_t wlen = 0;
        while (p[wlen] && p[wlen] != ' ' && p[wlen] != '\t' &&
               p[wlen] != '\n')
            wlen++;


        size_t need = line_len + (line_len ? 1 : 0) + wlen;
        bool fits = false;
        if (need < sizeof(line))
        {
            char probe[1024];
            size_t at = 0;
            if (line_len)
            {
                memcpy(probe, line, line_len);
                at = line_len;
                probe[at++] = ' ';
            }
            memcpy(probe + at, w, wlen);
            probe[at + wlen] = '\0';
            fits = dialog_measure(gfx, window_id, font, probe,
                                  at + wlen) <= (float)max_w;
        }
        if (fits)
        {
            if (line_len)
                line[line_len++] = ' ';
            memcpy(line + line_len, w, wlen);
            line_len += wlen;
            p += wlen;
            continue;
        }

        if (line_len > 0)
        {
            line[line_len] = '\0';
            if (line_no + 1 >= max_lines)
            {
                truncated = true;
                break;
            }
            gfx->render_text(window_id, font, line, x,
                             y_top + line_no * line_h, color, 1.0f);
            line_no++;
            line_len = 0;
            continue;
        }

        size_t used = 0;
        while (used < wlen && line_no < max_lines)
        {
            size_t take = 0;
            size_t step = 0;
            while (used + take < wlen)
            {
                step = dialog_utf8_step(w + used + take);
                if (used + take + step > wlen)
                    step = wlen - used - take;
                if (dialog_measure(gfx, window_id, font, w + used,
                                   take + step) > (float)max_w)
                    break;
                take += step;
            }
            if (take == 0)
                take = dialog_utf8_step(w + used);
            if (used + take < wlen)
            {

                memcpy(line, w + used, take);
                line[take] = '\0';
                if (line_no + 1 >= max_lines)
                {
                    line_len = take;
                    truncated = true;
                    used = wlen;
                    break;
                }
                gfx->render_text(window_id, font, line, x,
                                 y_top + line_no * line_h, color, 1.0f);
                line_no++;
                used += take;
            }
            else
            {
                memcpy(line, w + used, take);
                line_len = take;
                used = wlen;
            }
        }
        p += wlen;
    }
    if (line_len > 0 && line_no < max_lines)
    {
        line[line_len] = '\0';
        if (truncated || *p)
        {

            while (line_len > 0 &&
                   dialog_measure(gfx, window_id, font, line, line_len) +
                           dialog_measure(gfx, window_id, font, "...", 3) >
                       (float)max_w)
                line_len--;
            memcpy(line + line_len, "...", 3);
            line_len += 3;
            line[line_len] = '\0';
        }
        gfx->render_text(window_id, font, line, x,
                         y_top + line_no * line_h, color, 1.0f);
    }
}

void aroma_dialog_draw(AromaNode *dialog_node, size_t window_id)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;
    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (!dlg->visible || aroma_node_is_hidden(dialog_node))
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    __dialog_update_rect(dlg);
    __dialog_recompute_action_layout(dlg, gfx, window_id);

    if (dlg->content_node)
    {
        const int padding = dialog_dp(AROMA_DIALOG_PADDING_DP);
        aroma_container_set_rect(dlg->content_node,
                                 dlg->rect.x + padding,
                                 dlg->rect.y + dlg->content_y_offset,
                                 dlg->rect.width - padding * 2,
                                 dlg->action_button_y - (dlg->rect.y + dlg->content_y_offset) - dialog_dp(AROMA_DIALOG_SPACING_DP));
    }

    AromaTheme theme = aroma_theme_get_global();
    int x = dlg->rect.x, y = dlg->rect.y;

    {
        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
        int win_w = 0, win_h = 0;
        if (platform && platform->get_window_size)
            platform->get_window_size(window_id, &win_w, &win_h);
        if (win_w > 0 && win_h > 0)
            gfx->fill_rectangle(window_id, 0, 0, win_w, win_h, 0x80000000u, false, 0.0f);
    }
    gfx->fill_rectangle(window_id, x, y, dlg->rect.width, dlg->rect.height, theme.colors.surface, true, dialog_dp_f((float)AROMA_DIALOG_CORNER_RADIUS_DP));
    int hairline = dialog_dp(1);
    if (hairline < 1) hairline = 1;
    gfx->draw_hollow_rectangle(window_id, x, y, dlg->rect.width, dlg->rect.height, theme.colors.border, hairline, true, dialog_dp_f((float)AROMA_DIALOG_CORNER_RADIUS_DP));

    if (dlg->font && gfx->render_text)
    {
        gfx->render_text(window_id, dlg->font, dlg->title, x + dialog_dp(AROMA_DIALOG_TITLE_X_DP), y + dialog_dp(AROMA_DIALOG_TITLE_Y_DP), theme.colors.text_primary, 1.25f);
        int line_h = aroma_font_get_line_height(dlg->font);
        if (line_h <= 0)
            line_h = dialog_dp(AROMA_DIALOG_FONT_H_DP);
        int title_h = (line_h * 5) / 4;
        int msg_top = y + dialog_dp(AROMA_DIALOG_TITLE_Y_DP) + title_h + dialog_dp(AROMA_DIALOG_GAP_MED_DP);
        int msg_bottom = dlg->action_button_y - dialog_dp(AROMA_DIALOG_GAP_SMALL_DP);
        int max_lines = (msg_bottom > msg_top)
                            ? (msg_bottom - msg_top) / line_h
                            : 0;
        if (max_lines < 1)
            max_lines = 1;
        int max_w = dlg->rect.width - dialog_dp(AROMA_DIALOG_MESSAGE_W_DP);
        int min_w = dialog_dp(AROMA_DIALOG_MESSAGE_W_DP) + dialog_dp(AROMA_DIALOG_SPACING_DP);
        if (max_w < min_w)
            max_w = min_w;
        if (gfx->measure_text)
            dialog_draw_wrapped(gfx, window_id, dlg->font, dlg->message,
                                x + dialog_dp(AROMA_DIALOG_TITLE_X_DP), msg_top, max_w, line_h, max_lines,
                                theme.colors.text_secondary);
        else
            gfx->render_text(window_id, dlg->font, dlg->message, x + dialog_dp(AROMA_DIALOG_TITLE_X_DP), msg_top,
                             theme.colors.text_secondary, 1.0f);
    }

    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int bx = dlg->action_button_x[i];
        int by = dlg->action_button_y;
        int bw = dlg->action_button_widths[i];
        int bh = dlg->action_button_height;

        gfx->fill_rectangle(window_id, bx, by, bw, bh, theme.colors.primary_light, true, dialog_dp_f((float)AROMA_DIALOG_BUTTON_RADIUS_DP));
        gfx->draw_hollow_rectangle(window_id, bx, by, bw, bh, theme.colors.primary, hairline, true, dialog_dp_f((float)AROMA_DIALOG_BUTTON_RADIUS_DP));

        if (dlg->font && gfx->render_text)
        {
            int text_w = dlg->action_button_text_widths[i];
            if (text_w <= 0)
                text_w = dialog_dp(AROMA_DIALOG_TEXT_W_DP);
            int text_x = bx + (bw - text_w) / 2;
            int line_h = aroma_font_get_line_height(dlg->font);
            int text_y = by + (bh - line_h) / 2;
            gfx->render_text(window_id, dlg->font, dlg->actions[i].label, text_x, text_y, theme.colors.text_primary, 1.0f);
        }
    }
}

void aroma_dialog_destroy(AromaNode *dialog_node)
{
    if (!dialog_node)
        return;
    if (dialog_node->node_widget_ptr)
    {
        aroma_widget_free(dialog_node->node_widget_ptr);
        dialog_node->node_widget_ptr = NULL;
    }
    __destroy_node(dialog_node);
}
