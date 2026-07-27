#include "widgets/aroma_textbox.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_font.h"
#include "aroma_ui.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_TEXTBOX_PADDING_X 8

static uint64_t textbox_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static bool textbox_contains_point(AromaTextbox *tb, int x, int y)
{
    if (!tb)
        return false;
    return x >= tb->rect.x && x <= tb->rect.x + tb->rect.width &&
           y >= tb->rect.y && y <= tb->rect.y + tb->rect.height;
}

static void textbox_recompute_text_x(AromaTextbox *data)
{
    if (!data)
        return;
    data->text_x = data->rect.x + AROMA_TEXTBOX_PADDING_X;
}

static void textbox_insert_char(AromaTextbox *tb, char ch)
{
    if (!tb || tb->text_length >= AROMA_TEXTBOX_MAX_LENGTH - 1)
        return;

    // Shift characters right from cursor position
    memmove(&tb->text[tb->cursor_pos + 1], &tb->text[tb->cursor_pos],
            tb->text_length - tb->cursor_pos + 1);
    tb->text[tb->cursor_pos] = ch;
    tb->text_length++;
    tb->cursor_pos++;
    tb->text[tb->text_length] = '\0';
    
    tb->show_cursor = true;
    tb->cursor_blink_time = textbox_now_ms();

    if (tb->on_text_changed)
        tb->on_text_changed(NULL, tb->text, tb->user_data);
}

static void textbox_backspace(AromaTextbox *tb)
{
    if (!tb || tb->cursor_pos == 0 || tb->text_length == 0)
        return;

    // Move cursor back one position
    tb->cursor_pos--;
    
    // If there are characters after the deleted position, shift them left
    if (tb->cursor_pos < tb->text_length - 1) {
        memmove(&tb->text[tb->cursor_pos], 
                &tb->text[tb->cursor_pos + 1], 
                tb->text_length - tb->cursor_pos - 1);
    }
    
    // Update length and null-terminate
    tb->text_length--;
    tb->text[tb->text_length] = '\0';
    
    // Reset scroll offset if needed
    if (tb->scroll_offset > tb->cursor_pos)
        tb->scroll_offset = tb->cursor_pos;
    
    // Update cursor display
    tb->show_cursor = true;
    tb->cursor_blink_time = textbox_now_ms();

    if (tb->on_text_changed)
        tb->on_text_changed(NULL, tb->text, tb->user_data);
}

static void textbox_delete(AromaTextbox *tb)
{
    if (!tb || tb->cursor_pos >= tb->text_length)
        return;

    // If there are characters after the cursor, shift them left
    if (tb->cursor_pos < tb->text_length - 1) {
        memmove(&tb->text[tb->cursor_pos], 
                &tb->text[tb->cursor_pos + 1], 
                tb->text_length - tb->cursor_pos - 1);
    }
    
    // Update length and null-terminate
    tb->text_length--;
    tb->text[tb->text_length] = '\0';
    
    // Update cursor display
    tb->show_cursor = true;
    tb->cursor_blink_time = textbox_now_ms();

    if (tb->on_text_changed)
        tb->on_text_changed(NULL, tb->text, tb->user_data);
}

static float textbox_measure_range(AromaTextbox *tb, AromaGraphicsInterface *gfx,
                                   size_t window_id, size_t start, size_t end)
{
    if (!tb)
        return 0.0f;
    if (end > tb->text_length)
        end = tb->text_length;
    if (start > end)
        start = end;

    size_t length = end - start;
    if (length == 0)
        return 0.0f;

    if (!gfx || !gfx->measure_text || !tb->font || window_id == SIZE_MAX)
        return (float)(length * 8);

    if (length >= AROMA_TEXTBOX_MAX_LENGTH)
        length = AROMA_TEXTBOX_MAX_LENGTH - 1;

    char buffer[AROMA_TEXTBOX_MAX_LENGTH];
    memcpy(buffer, tb->text + start, length);
    buffer[length] = '\0';

    return gfx->measure_text(window_id, tb->font, buffer, tb->text_scale);
}

static void textbox_ensure_cursor_visible(AromaTextbox *data, AromaGraphicsInterface *gfx,
                                          size_t window_id, int available_width)
{
    if (available_width <= 0)
        return;

    if (data->scroll_offset > data->cursor_pos)
        data->scroll_offset = data->cursor_pos;

    float w = textbox_measure_range(data, gfx, window_id, data->scroll_offset, data->cursor_pos);

    while (w > (float)available_width && data->scroll_offset < data->cursor_pos)
    {
        data->scroll_offset++;
        w = textbox_measure_range(data, gfx, window_id, data->scroll_offset, data->cursor_pos);
    }

    while (data->scroll_offset > 0)
    {
        w = textbox_measure_range(data, gfx, window_id, data->scroll_offset - 1, data->cursor_pos);
        if (w > (float)available_width)
            break;
        data->scroll_offset--;
    }
}

AromaNode *aroma_textbox_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
    {
        LOG_ERROR("Invalid textbox parameters\n");
        return NULL;
    }



#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif


    AromaTextbox *data = (AromaTextbox *)aroma_widget_alloc(sizeof(AromaTextbox));
    if (!data)
    {
        LOG_ERROR("Failed to allocate textbox\n");
        return NULL;
    }

    memset(data, 0, sizeof(AromaTextbox));

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->text[0] = '\0';
    data->placeholder[0] = '\0';
    data->show_cursor = true;
    data->cursor_blink_time = textbox_now_ms();
    data->text_scale = 1.0f;
    data->last_window_id = SIZE_MAX;
    data->use_theme_colors = true;

    textbox_recompute_text_x(data);

    AromaTheme theme = aroma_theme_get_global();
    data->bg_color = theme.colors.surface;
    data->hover_bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f);
    data->focused_bg_color = theme.colors.surface;
    data->text_color = theme.colors.text_primary;
    data->border_color = theme.colors.border;
    data->hover_border_color = aroma_color_blend(theme.colors.border, theme.colors.primary, 0.35f);
    data->focused_border_color = theme.colors.primary;
    data->cursor_color = 0x000000;
    data->placeholder_color = 0x999999;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node)
    {
        LOG_ERROR("Failed to create textbox node\n");
        aroma_widget_free(data);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_textbox_draw);
    aroma_node_set_z_index(node, 0);

    LOG_INFO("Textbox created: x=%d, y=%d, w=%d, h=%d\n", x, y, width, height);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_textbox_set_placeholder(AromaNode *node, const char *placeholder)
{
    if (!node || !node->node_widget_ptr || !placeholder)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    strncpy(data->placeholder, placeholder, AROMA_TEXTBOX_MAX_LENGTH - 1);
    data->placeholder[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';
    aroma_node_invalidate(node);
}

void aroma_textbox_set_text(AromaNode *node, const char *text)
{
    if (!node || !node->node_widget_ptr || !text)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    strncpy(data->text, text, AROMA_TEXTBOX_MAX_LENGTH - 1);
    data->text[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';
    data->text_length = strlen(data->text);
    data->cursor_pos = data->text_length;
    data->scroll_offset = 0;
    data->show_cursor = true;
    data->cursor_blink_time = textbox_now_ms();

    if (data->on_text_changed)
        data->on_text_changed(node, data->text, data->user_data);

    aroma_node_invalidate(node);
}

const char *aroma_textbox_get_text(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return "";
    return ((AromaTextbox *)node->node_widget_ptr)->text;
}

void aroma_textbox_set_font(AromaNode *node, AromaFont *font)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    data->font = font;
    aroma_node_invalidate(node);
}

static AromaNode *g_focused_textbox = NULL;

void aroma_textbox_set_focused(AromaNode *node, bool focused)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;

    if (data->is_focused == focused)
        return;

    if (focused)
    {
        if (g_focused_textbox && g_focused_textbox != node)
        {
            AromaTextbox *old = (AromaTextbox *)g_focused_textbox->node_widget_ptr;
            if (old && old->is_focused)
            {
                old->is_focused = false;
                old->show_cursor = false;
                aroma_node_invalidate(g_focused_textbox);
                if (old->on_focus_changed)
                    old->on_focus_changed(g_focused_textbox, false, old->user_data);
            }
        }

        AromaNode *ui_focused = aroma_ui_get_focused_node();
        if (ui_focused && ui_focused != node && ui_focused != g_focused_textbox)
        {
            AromaTextbox *old = (AromaTextbox *)ui_focused->node_widget_ptr;
            if (old && old->is_focused)
            {
                old->is_focused = false;
                old->show_cursor = false;
                aroma_node_invalidate(ui_focused);
                if (old->on_focus_changed)
                    old->on_focus_changed(ui_focused, false, old->user_data);
            }
        }

        data->is_focused = true;
        data->show_cursor = true;
        data->cursor_blink_time = textbox_now_ms();
        g_focused_textbox = node;
        aroma_ui_set_focused_node(node);
    }
    else
    {
        data->is_focused = false;
        data->show_cursor = false;

        if (g_focused_textbox == node)
        {
            g_focused_textbox = NULL;
        }

        AromaNode *ui_focused = aroma_ui_get_focused_node();
        if (ui_focused == node)
        {
            aroma_ui_clear_focused_node(node);
        }
    }

    aroma_node_invalidate(node);

    if (data->on_focus_changed)
        data->on_focus_changed(node, focused, data->user_data);
}

bool aroma_textbox_is_focused(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return false;
    return ((AromaTextbox *)node->node_widget_ptr)->is_focused;
}

void aroma_textbox_set_on_text_changed(AromaNode *node,
                                       bool (*callback)(AromaNode *, const char *, void *),
                                       void *user_data)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    data->on_text_changed = callback;
    data->user_data = user_data;
}

void aroma_textbox_set_on_focus_changed(AromaNode *node,
                                        bool (*callback)(AromaNode *, bool, void *),
                                        void *user_data)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    data->on_focus_changed = callback;
    data->user_data = user_data;
}

void aroma_textbox_on_click(AromaNode *node, int mouse_x, int mouse_y)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;

    if (textbox_contains_point(data, mouse_x, mouse_y))
    {
        aroma_textbox_set_focused(node, true);
    }
    else
    {
        aroma_textbox_set_focused(node, false);
    }
}

void aroma_textbox_on_char(AromaNode *node, char character)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    if (!data->is_focused)
        return;

    // Filter out control characters
    if (character >= 32 && character <= 126) {
        textbox_insert_char(data, character);
        aroma_node_invalidate(node);
    }
}

void aroma_textbox_on_backspace(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    if (!data->is_focused)
        return;

    textbox_backspace(data);
    aroma_node_invalidate(node);
}

void aroma_textbox_on_delete(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    if (!data->is_focused)
        return;

    textbox_delete(data);
    aroma_node_invalidate(node);
}

void aroma_textbox_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(node))
        return;

    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    textbox_recompute_text_x(data);

    if (data->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        data->hover_bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f);
        data->focused_bg_color = theme.colors.surface;
        data->text_color = theme.colors.text_primary;
        data->border_color = theme.colors.border;
        data->hover_border_color = aroma_color_blend(theme.colors.border, theme.colors.primary, 0.35f);
        data->focused_border_color = theme.colors.primary;
        data->bg_color = theme.colors.surface;
    }

    uint32_t fill_color = data->bg_color;
    if (data->is_focused)
        fill_color = data->focused_bg_color;
    else if (data->is_hovered)
        fill_color = data->hover_bg_color;

    uint32_t border_color = data->border_color;
    int border_width = 1;
    if (data->is_focused)
    {
        border_color = data->focused_border_color;
        border_width = 2;
    }
    else if (data->is_hovered)
    {
        border_color = data->hover_border_color;
    }

    gfx->fill_rectangle(window_id, data->rect.x, data->rect.y,
                        data->rect.width, data->rect.height,
                        fill_color, true, 4.0f);

    gfx->draw_hollow_rectangle(window_id, data->rect.x, data->rect.y,
                               data->rect.width, data->rect.height,
                               border_color, border_width, true, 4.0f);

    if (data->font && gfx->render_text)
    {
        const char *text = data->text;
        uint32_t text_color = data->text_color;
        bool showing_placeholder = (!text || text[0] == '\0');

        if (showing_placeholder)
        {
            text = data->placeholder;
            text_color = data->placeholder_color;
        }

        int available_width = data->rect.width - (AROMA_TEXTBOX_PADDING_X * 2);
        if (available_width < 0)
            available_width = 0;

        char display_text[AROMA_TEXTBOX_MAX_LENGTH];

        if (data->is_focused && !showing_placeholder)
        {
            textbox_ensure_cursor_visible(data, gfx, window_id, available_width);

            size_t start = data->scroll_offset;
            size_t end = data->text_length;

            if (gfx->measure_text && available_width > 0)
            {
                while (end > start &&
                       textbox_measure_range(data, gfx, window_id, start, end) > (float)available_width)
                {
                    end--;
                }
            }

            size_t visible_len = end - start;
            if (visible_len >= sizeof(display_text))
                visible_len = sizeof(display_text) - 1;
            memcpy(display_text, data->text + start, visible_len);
            display_text[visible_len] = '\0';
        }
        else
        {
            strncpy(display_text, text ? text : "", sizeof(display_text) - 1);
            display_text[sizeof(display_text) - 1] = '\0';

            if (gfx->measure_text && available_width > 0)
            {
                float full_width = gfx->measure_text(window_id, data->font, display_text, data->text_scale);

                if (full_width > (float)available_width)
                {
                    const char *ellipsis = "...";
                    float ew = gfx->measure_text(window_id, data->font, ellipsis, data->text_scale);
                    size_t len = strlen(display_text);

                    while (len > 0)
                    {
                        display_text[len - 1] = '\0';
                        float w = gfx->measure_text(window_id, data->font, display_text, data->text_scale);
                        if (w + ew <= (float)available_width)
                        {
                            strncat(display_text, ellipsis, sizeof(display_text) - strlen(display_text) - 1);
                            break;
                        }
                        len--;
                    }
                }
            }
        }

        int baseline = data->rect.y + (data->rect.height - aroma_font_get_line_height(data->font)) / 2;
        gfx->render_text(window_id, data->font, display_text, data->text_x, baseline, text_color, data->text_scale);
    }

    if (data->is_focused)
    {
        uint64_t now = textbox_now_ms();
        if (data->cursor_blink_time == 0)
            data->cursor_blink_time = now;

        if (now != 0 && data->cursor_blink_time != 0 &&
            now - data->cursor_blink_time >= AROMA_TEXTBOX_CURSOR_BLINK_RATE)
        {
            data->cursor_blink_time = now;
            data->show_cursor = !data->show_cursor;
            aroma_node_invalidate(node);
        }

        if (data->show_cursor)
        {
            int cursor_x = data->text_x;

            if (data->font && gfx->measure_text && data->cursor_pos > data->scroll_offset)
            {
                char temp[AROMA_TEXTBOX_MAX_LENGTH];
                size_t start = data->scroll_offset;
                size_t end = data->cursor_pos;
                size_t len = end - start;

                if (len >= sizeof(temp))
                    len = sizeof(temp) - 1;
                memcpy(temp, data->text + start, len);
                temp[len] = '\0';

                float text_width = gfx->measure_text(window_id, data->font, temp, data->text_scale);
                cursor_x = data->text_x + (int)(text_width + 0.5f);
            }

            if (cursor_x > data->rect.x + data->rect.width - 2)
                cursor_x = data->rect.x + data->rect.width - 2;

            int cursor_height;
            if (data->font)
            {
                int line_height = aroma_font_get_line_height(data->font);
                cursor_height = line_height > 0 ? line_height : data->rect.height - 4;
            }
            else
            {
                cursor_height = data->rect.height - 4;
            }
            if (cursor_height < 2)
                cursor_height = 2;

            int cursor_y = data->rect.y + (data->rect.height - cursor_height) / 2;

            gfx->fill_rectangle(window_id, cursor_x, cursor_y, 2, cursor_height,
                                data->cursor_color, false, 0.0f);
        }
    }

    data->last_window_id = window_id;
}

void aroma_textbox_destroy(AromaNode *node)
{
    if (!node)
        return;

    if (g_focused_textbox == node)
    {
        g_focused_textbox = NULL;
    }

    if (node->node_widget_ptr)
    {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }

    aroma_ui_clear_focused_node(node);
}

static bool textbox_mouse_handler(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node || !event->target_node->node_widget_ptr)
        return false;

    AromaTextbox *tb = (AromaTextbox *)event->target_node->node_widget_ptr;
  
 int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;
                          AromaNode *cur = event->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int scroll_x, scroll_y;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_x += scroll_x;
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }
    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_CLICK:
    {
        bool inside = textbox_contains_point(tb, adjusted_x, adjusted_y);

        if (inside)
        {
            aroma_textbox_set_focused(event->target_node, true);

            AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
            size_t window_id = tb->last_window_id;

            int relative_x = adjusted_x - (tb->rect.x + AROMA_TEXTBOX_PADDING_X);
            if (relative_x <= 0)
            {
                tb->cursor_pos = tb->scroll_offset;
            }
            else if (!gfx || !gfx->measure_text || !tb->font || window_id == SIZE_MAX || tb->text_length == 0)
            {
                size_t approx = (size_t)(relative_x / 8);
                if (approx > tb->text_length)
                    approx = tb->text_length;
                tb->cursor_pos = approx;
            }
            else
            {
                size_t start_offset = tb->scroll_offset;
                float accumulated = 0.0f;
                tb->cursor_pos = tb->text_length;

                for (size_t i = start_offset; i < tb->text_length; ++i)
                {
                    char glyph[2] = {tb->text[i], '\0'};
                    float advance = gfx->measure_text(window_id, tb->font, glyph, tb->text_scale);
                    float midpoint = accumulated + (advance * 0.5f);

                    if ((float)relative_x < midpoint)
                    {
                        tb->cursor_pos = i;
                        break;
                    }
                    accumulated += advance;
                    if ((float)relative_x < accumulated)
                    {
                        tb->cursor_pos = i + 1;
                        break;
                    }
                }
            }

            tb->show_cursor = true;
            tb->cursor_blink_time = textbox_now_ms();
        }
        else
        {
            aroma_textbox_set_focused(event->target_node, false);
        }

        aroma_node_invalidate(event->target_node);

        if (user_data)
        {
            void (*cb)(void *) = (void (*)(void *))user_data;
            cb(NULL);
        }
        return true;
    }

    case EVENT_TYPE_MOUSE_MOVE:
    {
        bool inside = textbox_contains_point(tb, adjusted_x, adjusted_y);
        if (tb->is_hovered != inside)
        {
            tb->is_hovered = inside;
            aroma_node_invalidate(event->target_node);
        }
        return inside;
    }

    case EVENT_TYPE_MOUSE_ENTER:
        tb->is_hovered = true;
        aroma_node_invalidate(event->target_node);
        return true;

    case EVENT_TYPE_MOUSE_EXIT:
        tb->is_hovered = false;
        aroma_node_invalidate(event->target_node);
        return false;

    case EVENT_TYPE_MOUSE_RELEASE:
    {
        bool inside = textbox_contains_point(tb, adjusted_x, adjusted_y);
        if (tb->is_hovered != inside)
        {
            tb->is_hovered = inside;
            aroma_node_invalidate(event->target_node);
        }
        return inside;
    }

    default:
        break;
    }

    return false;
}

static bool textbox_keyboard_handler(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node || !event->target_node->node_widget_ptr)
        return false;

    AromaTextbox *tb = (AromaTextbox *)event->target_node->node_widget_ptr;
    if (!tb->is_focused)
        return false;

    if (event->event_type == EVENT_TYPE_KEY_PRESS)
    {
        uint32_t key = event->data.key.key_code;
        
        // Debug logging
        LOG_INFO("Textbox key event: keycode=0x%04X\n", key);

        // Handle backspace (multiple possible keycodes)
        if (key == 8 || key == 127 || key == 0xFF08 || key == 0x0008 || key == 0x007F)
        {
            LOG_INFO("Textbox: Backspace pressed\n");
            textbox_backspace(tb);
            aroma_node_invalidate(event->target_node);
            if (user_data)
            {
                void (*cb)(void *) = (void (*)(void *))user_data;
                cb(NULL);
            }
            return true;
        }

        // Handle delete (multiple possible keycodes)
        if (key == 0xFFFF || key == 0xFF9F || key == 0x007F || key == 127)
        {
            LOG_INFO("Textbox: Delete pressed\n");
            textbox_delete(tb);
            aroma_node_invalidate(event->target_node);
            if (user_data)
            {
                void (*cb)(void *) = (void (*)(void *))user_data;
                cb(NULL);
            }
            return true;
        }

        // Handle left arrow
        if (key == 0xFF51 || key == 0xFF52 || key == 0x0025)
        {
            LOG_INFO("Textbox: Left arrow pressed\n");
            if (tb->cursor_pos > 0)
            {
                tb->cursor_pos--;
                tb->show_cursor = true;
                tb->cursor_blink_time = textbox_now_ms();
                aroma_node_invalidate(event->target_node);
            }
            return true;
        }

        // Handle right arrow
        if (key == 0xFF53 || key == 0xFF54 || key == 0x0027)
        {
            LOG_INFO("Textbox: Right arrow pressed\n");
            if (tb->cursor_pos < tb->text_length)
            {
                tb->cursor_pos++;
                tb->show_cursor = true;
                tb->cursor_blink_time = textbox_now_ms();
                aroma_node_invalidate(event->target_node);
            }
            return true;
        }

        // Handle Home key
        if (key == 0xFF50 || key == 0x0024)
        {
            LOG_INFO("Textbox: Home key pressed\n");
            tb->cursor_pos = 0;
            tb->scroll_offset = 0;
            tb->show_cursor = true;
            tb->cursor_blink_time = textbox_now_ms();
            aroma_node_invalidate(event->target_node);
            return true;
        }

        // Handle End key
        if (key == 0xFF57 || key == 0x0023)
        {
            LOG_INFO("Textbox: End key pressed\n");
            tb->cursor_pos = tb->text_length;
            tb->show_cursor = true;
            tb->cursor_blink_time = textbox_now_ms();
            aroma_node_invalidate(event->target_node);
            return true;
        }

        // Handle Caps Lock
        if (key == 0xFFE5)
        {
            tb->caps_lock_on = !tb->caps_lock_on;
            aroma_node_invalidate(event->target_node);
            return true;
        }

        // Handle printable characters
        if (key >= 32 && key <= 126)
        {
            char ch = (char)(key & 0xFF);
            if (tb->caps_lock_on)
                ch = (char)toupper(ch);
            textbox_insert_char(tb, ch);
            aroma_node_invalidate(event->target_node);
            if (user_data)
            {
                void (*cb)(void *) = (void (*)(void *))user_data;
                cb(NULL);
            }
            return true;
        }

        // Handle Enter key
        if (key == 13 || key == 10 || key == 0xFF0D)
        {
            aroma_textbox_set_focused(event->target_node, false);
            return true;
        }
    }

    return false;
}

static bool textbox_focus_handler(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;

    if (event->event_type == EVENT_TYPE_FOCUS_GAINED)
    {
        aroma_textbox_set_focused(event->target_node, true);
        return true;
    }

    return false;
}

bool aroma_textbox_setup_events(AromaNode *textbox_node,
                                void (*on_redraw_callback)(void *),
                                bool (*on_text_changed_callback)(AromaNode *, const char *, void *),
                                void *user_data)
{
    if (!textbox_node)
        return false;

    if (on_text_changed_callback)
    {
        aroma_textbox_set_on_text_changed(textbox_node, on_text_changed_callback, user_data);
    }

    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_CLICK,
                          textbox_mouse_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_RELEASE,
                          textbox_mouse_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_MOVE,
                          textbox_mouse_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_ENTER,
                          textbox_mouse_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_EXIT,
                          textbox_mouse_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_KEY_PRESS,
                          textbox_keyboard_handler, (void *)on_redraw_callback, 10);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_FOCUS_GAINED,
                          textbox_focus_handler, (void *)on_redraw_callback, 10);

    return true;
}