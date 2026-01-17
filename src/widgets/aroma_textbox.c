#include "widgets/aroma_textbox.h"
#include "aroma_common.h"
#include "aroma_event.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    data->show_cursor = true;
    data->cursor_blink_time = 0;
    data->bg_color = 0xFFFFFF;          

    data->text_color = 0x000000;        

    data->border_color = 0xCCCCCC;      

    data->cursor_color = 0x000000;      

    data->placeholder_color = 0x999999; 

    data->placeholder[0] = '\0';
    data->on_text_changed = NULL;
    data->on_focus_changed = NULL;
    data->user_data = NULL;
    data->font = NULL;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, data);
    if (!node) {
        LOG_ERROR("Failed to create textbox node\n");
        aroma_widget_free(data);
        return NULL;
    }

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

    if (data->on_text_changed) {
        data->on_text_changed(node, data->text, data->user_data);
    }

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
        data->cursor_blink_time = 0;

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

    if (mouse_x >= data->rect.x && mouse_x <= data->rect.x + data->rect.width &&
        mouse_y >= data->rect.y && mouse_y <= data->rect.y + data->rect.height) {

        aroma_textbox_set_focused(node, true);

        data->cursor_pos = data->text_length;
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

        if (data->on_text_changed) {
            data->on_text_changed(node, data->text, data->user_data);
        }
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

        if (data->on_text_changed) {
            data->on_text_changed(node, data->text, data->user_data);
        }
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

void aroma_textbox_set_font(AromaNode* node, void* font)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    data->font = font;
}

void aroma_textbox_draw(AromaNode* node, size_t window_id)
{
    if (!node || !node->node_widget_ptr) {
        return;
    }

    AromaTextbox* data = (AromaTextbox*)node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        return;
    }

    gfx->fill_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height,
                       data->bg_color, false, 0.0f);

    gfx->draw_hollow_rectangle(window_id, data->rect.x, data->rect.y, data->rect.width, data->rect.height,
                              data->border_color, 2, false, 0.0f);

    if (gfx->render_text && data->font) {
        if (data->text_length > 0) {

            gfx->render_text(window_id, data->font, data->text, 
                           data->rect.x + 5, data->rect.y + 25, data->text_color);
        } else if (data->placeholder[0] != '\0') {

            gfx->render_text(window_id, data->font, data->placeholder, 
                           data->rect.x + 5, data->rect.y + 25, data->placeholder_color);
        }
    }

    if (data->is_focused) {

        uint64_t current_time = (uint64_t)time(NULL) * 1000;  

        if (data->cursor_blink_time == 0) {
            data->cursor_blink_time = current_time;
        }

        uint64_t elapsed = current_time - data->cursor_blink_time;
        data->show_cursor = (elapsed / AROMA_TEXTBOX_CURSOR_BLINK_RATE) % 2 == 0;

        if (data->show_cursor) {

            int cursor_x = data->rect.x + 5 + (data->cursor_pos * 8);  

            int cursor_y = data->rect.y + 5;
            int cursor_height = data->rect.height - 10;
            gfx->fill_rectangle(window_id, cursor_x, cursor_y, 2, cursor_height,
                               data->cursor_color, false, 0.0f);
        }
    }
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
}

static bool __textbox_default_mouse_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            aroma_textbox_on_click(event->target_node, 
                event->data.mouse.x, event->data.mouse.y);
            if (user_data) {
                void (*on_redraw)(void*) = (void (*)(void*))user_data;
                on_redraw(NULL);
            }
            return true;
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
                    if (user_data) {
                        void (*on_redraw)(void*) = (void (*)(void*))user_data;
                        on_redraw(NULL);
                    }
                    return true;
                }

                if (key_code < 32 || key_code > 255) {
                    return true;
                }

                if (key_code >= 32 && key_code <= 126) {
                    char character = (char)(key_code & 0xFF);
                    aroma_textbox_on_char(event->target_node, character);
                    if (user_data) {
                        void (*on_redraw)(void*) = (void (*)(void*))user_data;
                        on_redraw(NULL);
                    }
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

    aroma_event_subscribe(textbox_node->node_id, EVENT_TYPE_KEY_PRESS, __textbox_default_keyboard_handler, (void*)on_redraw_callback, 100);

    return true;
}

