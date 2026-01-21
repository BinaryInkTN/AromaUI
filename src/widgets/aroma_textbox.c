/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AROMA_TEXTBOX_PADDING_X 8

static uint64_t __textbox_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static bool __textbox_contains_point(const AromaTextbox* textbox, int x, int y)
{
    if (!textbox) {
        return false;
    }
    return x >= textbox->rect.x && x <= textbox->rect.x + textbox->rect.width &&
           y >= textbox->rect.y && y <= textbox->rect.y + textbox->rect.height;
}

static void __textbox_request_redraw(void* user_data)
{
    if (!user_data) {
        return;
    }
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static void __textbox_update_hover(AromaTextbox* textbox, AromaNode* node, bool hovered, void* user_data)
{
    if (!textbox || textbox->is_hovered == hovered) {
        return;
    }

    textbox->is_hovered = hovered;
    aroma_node_invalidate(node);
    __textbox_request_redraw(user_data);
}

static float __textbox_measure_prefix(const AromaTextbox* textbox, AromaGraphicsInterface* gfx, size_t window_id, size_t length)
{
    if (!textbox || length == 0) {
        return 0.0f;
    }

    if (!gfx || !gfx->measure_text || !textbox->font || window_id == SIZE_MAX) {
        return (float)(length * 8);
    }

    if (length > textbox->text_length) {
        length = textbox->text_length;
    }

    char buffer[AROMA_TEXTBOX_MAX_LENGTH];
    memcpy(buffer, textbox->text, length);
    buffer[length] = '\0';
    return gfx->measure_text(window_id, textbox->font, buffer);
}

static size_t __textbox_cursor_from_click(const AromaTextbox* textbox, AromaGraphicsInterface* gfx,
                                          size_t window_id, int mouse_x)
{
    if (!textbox) {
        return 0;
    }

    int relative_x = mouse_x - (textbox->rect.x + AROMA_TEXTBOX_PADDING_X);
    if (relative_x <= 0) {
        return 0;
    }

    if (!gfx || !gfx->measure_text || !textbox->font || window_id == SIZE_MAX || textbox->text_length == 0) {
        size_t approx = (size_t)(relative_x / 8);
        if (approx > textbox->text_length) {
            approx = textbox->text_length;
        }
        return approx;
    }

    float accumulated = 0.0f;
    for (size_t i = 0; i < textbox->text_length; ++i) {
        char glyph[2] = { textbox->text[i], '\0' };
        float advance = gfx->measure_text(window_id, textbox->font, glyph);
        float midpoint = accumulated + (advance * 0.5f);
        if (relative_x < midpoint) {
            return i;
        }
        accumulated += advance;
        if (relative_x < accumulated) {
            return i + 1;
        }
    }

    return textbox->text_length;
}

AromaNode* aroma_textbox_create(AromaNode* parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid textbox parameters\n");
        return NULL;
    }

    AromaTextbox* data = (AromaTextbox*)aroma_widget_alloc(sizeof(AromaTextbox));
    if (!data) {
        LOG_ERROR("Failed to allocate textbox data\n");
        return NULL;
    }

    data->rect.x = x;
    data->rect.y = y;
    data->rect.width = width;
    data->rect.height = height;
    data->text[0] = '\0';
    data->text_length = 0;
    data->cursor_pos = 0;
    data->is_focused = false;
    data->is_hovered = false;
    data->show_cursor = true;
    data->cursor_blink_time = 0;
    AromaTheme theme = aroma_theme_get_global();
    data->hover_bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f);
    data->focused_bg_color = theme.colors.surface;
    data->text_color = theme.colors.text_primary;
    data->border_color = theme.colors.border;
    data->hover_border_color = aroma_color_adjust(theme.colors.border, 0.06f);
    data->focused_border_color = theme.colors.primary;
    data->cursor_color = 0x000000;
    data->placeholder_color = 0x999999;
    data->bg_color = theme.colors.surface;

    data->placeholder[0] = '\0';
    data->on_text_changed = NULL;
    data->on_focus_changed = NULL;
    data->user_data = NULL;
    data->font = NULL;
    data->last_window_id = SIZE_MAX;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node) {
        LOG_ERROR("Failed to create textbox node\n");
        aroma_widget_free(data);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_textbox_draw);

    LOG_INFO("Textbox created: x=%d, y=%d, w=%d, h=%d\n", x, y, width, height);

    return node;
}

void aroma_textbox_set_placeholder(AromaNode* node, const char* placeholder)
{
    if (!node || !node->node_widget_ptr || !placeholder) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    strncpy(data->placeholder, placeholder, AROMA_TEXTBOX_MAX_LENGTH - 1);
    data->placeholder[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';

    aroma_node_invalidate(node);

    LOG_INFO("Textbox placeholder set: %s\n", placeholder);
}

void aroma_textbox_set_text(AromaNode* node, const char* text)
{
    if (!node || !node->node_widget_ptr || !text) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    strncpy(data->text, text, AROMA_TEXTBOX_MAX_LENGTH - 1);
    data->text[AROMA_TEXTBOX_MAX_LENGTH - 1] = '\0';
    data->text_length = strlen(data->text);
    data->cursor_pos = data->text_length;
    data->show_cursor = true;
    data->cursor_blink_time = __textbox_now_ms();

    if (data->on_text_changed) {
        data->on_text_changed(node, data->text, data->user_data);
    }

    aroma_node_invalidate(node);

    LOG_INFO("Textbox text set: %s\n", text);
}

const char* aroma_textbox_get_text(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) {
        return "";
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    return data->text;
}

void aroma_textbox_set_focused(AromaNode* node, bool focused)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    if (data->is_focused != focused) {
        data->is_focused = focused;
        data->show_cursor = true;
        data->cursor_blink_time = __textbox_now_ms();

        if (focused) {
            aroma_ui_set_focused_node(node);
        } else {
            aroma_ui_clear_focused_node(node);
        }

        aroma_node_invalidate(node);

        if (data->on_focus_changed) {
            data->on_focus_changed(node, focused, data->user_data);
        }

        LOG_INFO("Textbox focus changed: %s\n", focused ? "focused" : "unfocused");
    }
}

bool aroma_textbox_is_focused(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) {
        return false;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    return data->is_focused;
}

void aroma_textbox_on_click(AromaNode* node, int mouse_x, int mouse_y)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;

    bool inside = __textbox_contains_point(data, mouse_x, mouse_y);
    if (inside) {
        aroma_textbox_set_focused(node, true);

        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        size_t window_id = data->last_window_id;
        size_t desired_cursor = __textbox_cursor_from_click(data, gfx, window_id, mouse_x);
        if (desired_cursor > data->text_length) {
            desired_cursor = data->text_length;
        }

        data->cursor_pos = desired_cursor;
        data->show_cursor = true;
        data->cursor_blink_time = __textbox_now_ms();
        aroma_node_invalidate(node);
    } else {
        aroma_textbox_set_focused(node, false);
    }
}

void aroma_textbox_on_char(AromaNode* node, char character)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;

    if (!data->is_focused) {
        return;
    }

    if (data->text_length < AROMA_TEXTBOX_MAX_LENGTH - 1) {

        memmove(&data->text[data->cursor_pos + 1], 
                &data->text[data->cursor_pos], 
                data->text_length - data->cursor_pos);
        data->text[data->cursor_pos] = character;
        data->text_length++;
        data->cursor_pos++;
        data->text[data->text_length] = '\0';
        data->show_cursor = true;
        data->cursor_blink_time = __textbox_now_ms();

        if (data->on_text_changed) {
            data->on_text_changed(node, data->text, data->user_data);
        }

        aroma_node_invalidate(node);
    }
}

void aroma_textbox_on_backspace(AromaNode* node)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;

    if (!data->is_focused) {
        return;
    }

    if (data->cursor_pos > 0) {
        data->cursor_pos--;
        memmove(&data->text[data->cursor_pos], 
                &data->text[data->cursor_pos + 1], 
                data->text_length - data->cursor_pos);
        data->text_length--;
        data->text[data->text_length] = '\0';
        data->show_cursor = true;
        data->cursor_blink_time = __textbox_now_ms();

        if (data->on_text_changed) {
            data->on_text_changed(node, data->text, data->user_data);
        }

        aroma_node_invalidate(node);
    }
}

void aroma_textbox_set_on_text_changed(AromaNode* node, 
                                      bool (*callback)(AromaNode*, const char*, void*), 
                                      void* user_data)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    data->on_text_changed = callback;
    data->user_data = user_data;

    LOG_INFO("Textbox on_text_changed callback registered\n");
}

void aroma_textbox_set_on_focus_changed(AromaNode* node, 
                                       bool (*callback)(AromaNode*, bool, void*), 
                                       void* user_data)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    data->on_focus_changed = callback;
    data->user_data = user_data;

    LOG_INFO("Textbox on_focus_changed callback registered\n");
}

void aroma_textbox_set_font(AromaNode* node, AromaFont* font)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    data->font = font;
    aroma_node_invalidate(node);
}

void aroma_textbox_draw(AromaNode* node, size_t window_id)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }
    if (aroma_node_is_hidden(node)) return;

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        return;
    }

    uint32_t fill_color = data->bg_color;
    if (data->is_focused) {
        fill_color = data->focused_bg_color;
    } else if (data->is_hovered) {
        fill_color = data->hover_bg_color;
    }

    uint32_t border_color = data->border_color;
    if (data->is_focused) {
        border_color = data->focused_border_color;
    } else if (data->is_hovered) {
        border_color = data->hover_border_color;
    }

    gfx->fill_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height,
                        fill_color, true, 4.0f);  // MD3 extra-small corner

    gfx->draw_hollow_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height,
                               border_color, data->is_focused ? 2 : 1, true, 4.0f);

    const int text_x = data->rect.x + AROMA_TEXTBOX_PADDING_X;
    int line_height = data->font ? aroma_font_get_line_height(data->font) : (data->rect.height - 4);
    if (line_height <= 0) {
        line_height = data->rect.height - 4;
    }
    int ascender = data->font ? aroma_font_get_ascender(data->font) : (line_height - 2);
    int baseline = data->rect.y + (data->rect.height - line_height) / 2 + ascender;

    if (data->font && gfx->render_text) {
        const char* text = data->text;
        uint32_t text_color = data->text_color;
        if (!text || text[0] == '\0') {
            text = data->placeholder;
            text_color = data->placeholder_color;
        }

        char display_text[AROMA_TEXTBOX_MAX_LENGTH];
        strncpy(display_text, text ? text : "", sizeof(display_text) - 1);
        display_text[sizeof(display_text) - 1] = '\0';

        int available_width = data->rect.width - (AROMA_TEXTBOX_PADDING_X * 2);
        if (available_width < 0) available_width = 0;

        if (gfx->measure_text && available_width > 0) {
            float full_width = gfx->measure_text(window_id, data->font, display_text);
            if (full_width > (float)available_width) {
                const char* ellipsis = "…";
                float ellipsis_width = gfx->measure_text(window_id, data->font, ellipsis);
                size_t len = strlen(display_text);
                while (len > 0) {
                    display_text[len - 1] = '\0';
                    float w = gfx->measure_text(window_id, data->font, display_text);
                    if (w + ellipsis_width <= (float)available_width) {
                        strncat(display_text, ellipsis, sizeof(display_text) - strlen(display_text) - 1);
                        break;
                    }
                    len--;
                }
            }
        }

        gfx->render_text(window_id, data->font, display_text, text_x, data->rect.y + line_height, text_color);
    }

    if (data->is_focused) {
        uint64_t now = __textbox_now_ms();
        if (data->cursor_blink_time == 0) {
            data->cursor_blink_time = now;
        }

        if (now != 0 && data->cursor_blink_time != 0 && now - data->cursor_blink_time >= AROMA_TEXTBOX_CURSOR_BLINK_RATE) {
            data->cursor_blink_time = now;
            data->show_cursor = !data->show_cursor;
            aroma_node_invalidate(node);
        }

        if (data->show_cursor) {
            float prefix_width = __textbox_measure_prefix(data, gfx, window_id, data->cursor_pos);
            int cursor_x = text_x + (int)(prefix_width + 0.5f);
            int cursor_height = line_height;
            if (cursor_height <= 0) {
                cursor_height = data->rect.height - 4;
            }
            if (cursor_height < 2) {
                cursor_height = 2;
            }
            int cursor_y = data->rect.y + (data->rect.height - cursor_height) / 2;

            if (cursor_x > data->rect.x + data->rect.width - 2) {
                cursor_x = data->rect.x + data->rect.width - 2;
            }

            gfx->fill_rectangle(window_id, cursor_x, cursor_y, 2, cursor_height,
                                data->cursor_color, false, 0.0f);
        }
    }

    data->last_window_id = window_id;
}

void aroma_textbox_destroy(AromaNode* node)
{
    if (!node) {
        return;
    }

    if (node->node_widget_ptr) {
        aroma_widget_free(node->node_widget_ptr);
        node->node_widget_ptr = NULL;
    }

    aroma_ui_clear_focused_node(node);
}

static bool __textbox_default_mouse_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    AromaTextbox* textbox = (AromaTextbox*)event->target_node->node_widget_ptr;
    if (!textbox) return false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_ENTER:
            __textbox_update_hover(textbox, event->target_node, true, user_data);
            return true;
        case EVENT_TYPE_MOUSE_EXIT:
            __textbox_update_hover(textbox, event->target_node, false, user_data);
            return false;
        case EVENT_TYPE_MOUSE_MOVE:
            {
                bool inside = __textbox_contains_point(textbox, event->data.mouse.x, event->data.mouse.y);
                __textbox_update_hover(textbox, event->target_node, inside, user_data);
                return inside;
            }
        case EVENT_TYPE_MOUSE_CLICK:
            aroma_textbox_on_click(event->target_node, 
                event->data.mouse.x, event->data.mouse.y);
            __textbox_request_redraw(user_data);
            return true;
        case EVENT_TYPE_MOUSE_RELEASE:
            {
                bool inside = __textbox_contains_point(textbox, event->data.mouse.x, event->data.mouse.y);
                __textbox_update_hover(textbox, event->target_node, inside, user_data);
                return inside;
            }
        default:
            break;
    }
    return false;
}

static bool __textbox_default_keyboard_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    AromaTextbox* textbox = (AromaTextbox*)event->target_node->node_widget_ptr;
    if (!textbox || !textbox->is_focused) return false;

    switch (event->event_type) {
        case EVENT_TYPE_KEY_PRESS:
            {
                uint32_t key_code = event->data.key.key_code;

                if (key_code == 8 || key_code == 127) {
                    aroma_textbox_on_backspace(event->target_node);
                    __textbox_request_redraw(user_data);
                    return true;
                }

                if (key_code == 0xFF51) {
                    if (textbox->cursor_pos > 0) {
                        textbox->cursor_pos--;
                        textbox->show_cursor = true;
                        textbox->cursor_blink_time = __textbox_now_ms();
                        aroma_node_invalidate(event->target_node);
                        __textbox_request_redraw(user_data);
                    }
                    return true;
                }

                if (key_code == 0xFF53) {
                    if (textbox->cursor_pos < textbox->text_length) {
                        textbox->cursor_pos++;
                        textbox->show_cursor = true;
                        textbox->cursor_blink_time = __textbox_now_ms();
                        aroma_node_invalidate(event->target_node);
                        __textbox_request_redraw(user_data);
                    }
                    return true;
                }

                if (key_code == 0xFF50) {
                    textbox->cursor_pos = 0;
                    textbox->show_cursor = true;
                    textbox->cursor_blink_time = __textbox_now_ms();
                    aroma_node_invalidate(event->target_node);
                    __textbox_request_redraw(user_data);
                    return true;
                }

                if (key_code == 0xFF57) {
                    textbox->cursor_pos = textbox->text_length;
                    textbox->show_cursor = true;
                    textbox->cursor_blink_time = __textbox_now_ms();
                    aroma_node_invalidate(event->target_node);
                    __textbox_request_redraw(user_data);
                    return true;
                }

                if (key_code < 32 || key_code > 255) {
                    return true;
                }

                if (key_code >= 32 && key_code <= 126) {
                    char character = (char)(key_code & 0xFF);
                    aroma_textbox_on_char(event->target_node, character);
                    __textbox_request_redraw(user_data);
                    return true;
                }
            }
            return false;
        default:
            break;
    }
    return false;
}

bool aroma_textbox_setup_events(AromaNode* textbox_node, 
                               void (*on_redraw_callback)(void*),
                               bool (*on_text_changed_callback)(AromaNode*, const char*, void*),
                               void* user_data)
{
    if (!textbox_node) return false;

    if (on_text_changed_callback) {
        aroma_textbox_set_on_text_changed(textbox_node, on_text_changed_callback, user_data);
    }

    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_CLICK, __textbox_default_mouse_handler, (void*)on_redraw_callback, 100);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __textbox_default_mouse_handler, (void*)on_redraw_callback, 100);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_MOVE, __textbox_default_mouse_handler, (void*)on_redraw_callback, 95);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_ENTER, __textbox_default_mouse_handler, (void*)on_redraw_callback, 95);
    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_MOUSE_EXIT, __textbox_default_mouse_handler, (void*)on_redraw_callback, 95);

    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_KEY_PRESS, __textbox_default_keyboard_handler, (void*)on_redraw_callback, 100);

    return true;
}

