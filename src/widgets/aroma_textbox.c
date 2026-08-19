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
#define AROMA_VK_KEY_WIDTH 60
#define AROMA_VK_KEY_HEIGHT 50
#define AROMA_VK_KEY_SPACING 8
#define AROMA_VK_ROW_SPACING 8
#define AROMA_VK_PADDING 20
#define AROMA_VK_PREVIEW_HEIGHT 70
#define AROMA_VK_PREVIEW_GAP 30
#define AROMA_VK_BUTTON_HEIGHT 50
#define AROMA_VK_BUTTON_GAP 20

static void textbox_insert_char(AromaTextbox *tb, char ch);
static void textbox_backspace(AromaTextbox *tb);
static void textbox_delete(AromaTextbox *tb);

void aroma_textbox_show_virtual_keyboard(AromaNode *node);
void aroma_textbox_hide_virtual_keyboard(AromaNode *node);
void aroma_textbox_enable_virtual_keyboard(AromaNode *node, bool enable);
bool aroma_textbox_is_virtual_keyboard_enabled(AromaNode *node);

static const char *vk_rows[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
    NULL
};

typedef struct {
    bool enabled;
    bool visible;
    int x;
    int y;
    int width;
    int height;
    AromaNode *textbox_node;
    AromaNode *keyboard_node;
    bool shift_pressed;
    bool caps_lock;
    int active_key;
    int hovered_key;
    int window_width;
    int window_height;
} AromaVirtualKeyboard;

static AromaVirtualKeyboard g_vk = {
    .enabled = false,
    .visible = false,
    .x = 0,
    .y = 0,
    .width = 0,
    .height = 0,
    .textbox_node = NULL,
    .keyboard_node = NULL,
    .shift_pressed = false,
    .caps_lock = false,
    .active_key = -1,
    .hovered_key = -1,
    .window_width = 800,
    .window_height = 480
};

typedef struct {
    AromaRect rect;
    AromaTextbox *textbox;
    bool shift_pressed;
    bool caps_lock;
    int active_key;
    int hovered_key;
    uint32_t bg_color;
    uint32_t key_color;
    uint32_t key_hover_color;
    uint32_t key_active_color;
    uint32_t text_color;
    uint32_t border_color;
    uint32_t preview_bg;
    uint32_t preview_text;
    uint32_t submit_color;
    uint32_t submit_text;
} AromaVKWidget;

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

static bool vk_contains_point(int x, int y)
{
    return x >= g_vk.x && x <= g_vk.x + g_vk.width &&
           y >= g_vk.y && y <= g_vk.y + g_vk.height;
}

static int vk_get_key_count(void)
{
    int count = 0;
    for (int i = 0; vk_rows[i] != NULL; i++) {
        count += strlen(vk_rows[i]);
    }
    return count;
}

static int vk_get_keys_start_y(void)
{
    return AROMA_VK_PADDING + AROMA_VK_PREVIEW_HEIGHT + AROMA_VK_PREVIEW_GAP;
}

static void vk_get_key_rect(int key_index, int *x, int *y, int *width, int *height)
{
    *x = 0;
    *y = 0;
    *width = AROMA_VK_KEY_WIDTH;
    *height = AROMA_VK_KEY_HEIGHT;
    
    int current_index = 0;
    for (int row = 0; vk_rows[row] != NULL; row++) {
        int row_length = strlen(vk_rows[row]);
        if (key_index >= current_index && key_index < current_index + row_length) {
            int col = key_index - current_index;
            
            int total_row_width = row_length * AROMA_VK_KEY_WIDTH + 
                                  (row_length - 1) * AROMA_VK_KEY_SPACING;
            int row_offset = (g_vk.width - total_row_width) / 2;
            
            *x = g_vk.x + AROMA_VK_PADDING + row_offset + 
                 col * (AROMA_VK_KEY_WIDTH + AROMA_VK_KEY_SPACING);
            *y = g_vk.y + vk_get_keys_start_y() + 
                 row * (AROMA_VK_KEY_HEIGHT + AROMA_VK_ROW_SPACING);
            return;
        }
        current_index += row_length;
    }
}

static int vk_get_special_y(void)
{
    return vk_get_keys_start_y() + 4 * (AROMA_VK_KEY_HEIGHT + AROMA_VK_ROW_SPACING);
}

static int vk_get_buttons_y(void)
{
    return vk_get_special_y() + AROMA_VK_KEY_HEIGHT + AROMA_VK_BUTTON_GAP;
}

static int vk_get_key_at_position(int x, int y)
{
    if (!vk_contains_point(x, y)) {
        return -1;
    }
    
    int total_keys = vk_get_key_count();
    for (int i = 0; i < total_keys; i++) {
        int key_x, key_y, key_width, key_height;
        vk_get_key_rect(i, &key_x, &key_y, &key_width, &key_height);
        
        if (x >= key_x && x <= key_x + key_width &&
            y >= key_y && y <= key_y + key_height) {
            return i;
        }
    }
    return -1;
}

static char vk_get_key_char(int key_index)
{
    if (key_index < 0) {
        return '\0';
    }
    
    int current_index = 0;
    for (int row = 0; vk_rows[row] != NULL; row++) {
        int row_length = strlen(vk_rows[row]);
        if (key_index >= current_index && key_index < current_index + row_length) {
            int col = key_index - current_index;
            char ch = vk_rows[row][col];
            
            if (g_vk.shift_pressed || g_vk.caps_lock) {
                if (ch >= 'a' && ch <= 'z') {
                    ch = toupper(ch);
                } else if (ch >= '0' && ch <= '9') {
                    const char *shift_symbols = ")!@#$%^&*(";
                    ch = shift_symbols[ch - '0'];
                }
            }
            return ch;
        }
        current_index += row_length;
    }
    return '\0';
}

static void vk_get_button_rects(AromaVKWidget *vk, int *submit_x, int *submit_y, 
                                 int *submit_w, int *submit_h,
                                 int *cancel_x, int *cancel_y, 
                                 int *cancel_w, int *cancel_h)
{
    int button_y = vk->rect.y + vk_get_buttons_y();
    
    *submit_w = 150;
    *submit_h = AROMA_VK_BUTTON_HEIGHT;
    *cancel_w = 100;
    *cancel_h = AROMA_VK_BUTTON_HEIGHT;
    
    *submit_x = vk->rect.x + vk->rect.width / 2 - *submit_w - 15;
    *submit_y = button_y;
    *cancel_x = vk->rect.x + vk->rect.width / 2 + 15;
    *cancel_y = button_y;
}

static void vk_draw_widget(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || !g_vk.visible)
        return;
    
    AromaVKWidget *vk = (AromaVKWidget *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    
    gfx->fill_rectangle(window_id, vk->rect.x, vk->rect.y, 
                        vk->rect.width, vk->rect.height, 
                        vk->bg_color, true, 16.0f);
    
    gfx->draw_hollow_rectangle(window_id, vk->rect.x, vk->rect.y, 
                               vk->rect.width, vk->rect.height, 
                               vk->border_color, 2, true, 16.0f);
    
    int preview_x = vk->rect.x + AROMA_VK_PADDING;
    int preview_y = vk->rect.y + AROMA_VK_PADDING;
    int preview_w = vk->rect.width - 2 * AROMA_VK_PADDING;
    int preview_h = AROMA_VK_PREVIEW_HEIGHT;
    
    gfx->fill_rectangle(window_id, preview_x, preview_y, preview_w, preview_h,
                        vk->preview_bg, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, preview_x, preview_y, preview_w, preview_h,
                               vk->border_color, 1, true, 10.0f);
    
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        const char *display_text = vk->textbox->text;
        if (!display_text || display_text[0] == '\0') {
            display_text = vk->textbox->placeholder;
        }
        
        int text_x = preview_x + 15;
        int text_y = preview_y + (preview_h - aroma_font_get_line_height(vk->textbox->font)) / 2;
        
        char preview_text[AROMA_TEXTBOX_MAX_LENGTH];
        strncpy(preview_text, display_text ? display_text : "", sizeof(preview_text) - 1);
        preview_text[sizeof(preview_text) - 1] = '\0';
        
        int max_width = preview_w - 30;
        if (gfx->measure_text) {
            float text_width = gfx->measure_text(window_id, vk->textbox->font, 
                                                 preview_text, vk->textbox->text_scale);
            if (text_width > max_width) {
                size_t len = strlen(preview_text);
                while (len > 0 && text_width > max_width) {
                    preview_text[--len] = '\0';
                    text_width = gfx->measure_text(window_id, vk->textbox->font, 
                                                   preview_text, vk->textbox->text_scale);
                }
                if (len > 3) {
                    strcpy(preview_text + len - 3, "...");
                }
            }
        }
        
        gfx->render_text(window_id, vk->textbox->font, preview_text, 
                        text_x, text_y, vk->preview_text, vk->textbox->text_scale);
    }
    
    int total_keys = vk_get_key_count();
    for (int i = 0; i < total_keys; i++) {
        int x, y, width, height;
        vk_get_key_rect(i, &x, &y, &width, &height);
        
        uint32_t bg_color = vk->key_color;
        if (i == vk->active_key) {
            bg_color = vk->key_active_color;
        } else if (i == vk->hovered_key) {
            bg_color = vk->key_hover_color;
        }
        
        gfx->fill_rectangle(window_id, x, y, width, height, bg_color, true, 10.0f);
        gfx->draw_hollow_rectangle(window_id, x, y, width, height, 
                                   vk->border_color, 1, true, 10.0f);
        
        char key_char = vk_get_key_char(i);
        if (key_char != '\0' && vk->textbox && vk->textbox->font && gfx->render_text) {
            char key_str[2] = {key_char, '\0'};
            int char_width = gfx->measure_text ? 
                (int)gfx->measure_text(window_id, vk->textbox->font, key_str, vk->textbox->text_scale) : 8;
            int char_height = aroma_font_get_line_height(vk->textbox->font);
            
            int char_x = x + (width - char_width) / 2;
            int char_y = y + (height - char_height) / 2;
            
            gfx->render_text(window_id, vk->textbox->font, key_str, char_x, char_y, 
                            vk->text_color, vk->textbox->text_scale);
        }
    }
    
    int special_y = vk->rect.y + vk_get_special_y();
    int special_x = vk->rect.x + AROMA_VK_PADDING;
    
    uint32_t shift_color = g_vk.shift_pressed ? vk->key_active_color : vk->key_color;
    gfx->fill_rectangle(window_id, special_x, special_y, 
                        AROMA_VK_KEY_WIDTH * 2, AROMA_VK_KEY_HEIGHT, 
                        shift_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, special_x, special_y, 
                               AROMA_VK_KEY_WIDTH * 2, AROMA_VK_KEY_HEIGHT, 
                               vk->border_color, 1, true, 10.0f);
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        gfx->render_text(window_id, vk->textbox->font, "Shift", 
                        special_x + 20, special_y + 15, vk->text_color, vk->textbox->text_scale);
    }
    
    int space_width = g_vk.width - AROMA_VK_PADDING * 2 - AROMA_VK_KEY_WIDTH * 4 - AROMA_VK_KEY_SPACING * 4;
    int space_x = special_x + AROMA_VK_KEY_WIDTH * 2 + AROMA_VK_KEY_SPACING;
    gfx->fill_rectangle(window_id, space_x, special_y, 
                        space_width, AROMA_VK_KEY_HEIGHT, vk->key_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, space_x, special_y, 
                               space_width, AROMA_VK_KEY_HEIGHT, vk->border_color, 1, true, 10.0f);
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        gfx->render_text(window_id, vk->textbox->font, "Space", 
                        space_x + space_width / 2 - 25, special_y + 15, 
                        vk->text_color, vk->textbox->text_scale);
    }
    
    int backspace_x = space_x + space_width + AROMA_VK_KEY_SPACING;
    int backspace_w = AROMA_VK_KEY_WIDTH * 2;
    gfx->fill_rectangle(window_id, backspace_x, special_y, 
                        backspace_w, AROMA_VK_KEY_HEIGHT, 
                        vk->key_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, backspace_x, special_y, 
                               backspace_w, AROMA_VK_KEY_HEIGHT, 
                               vk->border_color, 1, true, 10.0f);
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        gfx->render_text(window_id, vk->textbox->font, "Backspace", 
                        backspace_x + 15, special_y + 15, vk->text_color, vk->textbox->text_scale);
    }
    
    int submit_x, submit_y, submit_w, submit_h;
    int cancel_x, cancel_y, cancel_w, cancel_h;
    vk_get_button_rects(vk, &submit_x, &submit_y, &submit_w, &submit_h,
                       &cancel_x, &cancel_y, &cancel_w, &cancel_h);
    
    gfx->fill_rectangle(window_id, submit_x, submit_y, submit_w, submit_h,
                        vk->submit_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, submit_x, submit_y, submit_w, submit_h,
                               vk->border_color, 1, true, 10.0f);
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        gfx->render_text(window_id, vk->textbox->font, "Submit", 
                        submit_x + 40, submit_y + 15, vk->submit_text, vk->textbox->text_scale);
    }
    
    gfx->fill_rectangle(window_id, cancel_x, cancel_y, cancel_w, cancel_h,
                        vk->key_color, true, 10.0f);
    gfx->draw_hollow_rectangle(window_id, cancel_x, cancel_y, cancel_w, cancel_h,
                               vk->border_color, 1, true, 10.0f);
    if (vk->textbox && vk->textbox->font && gfx->render_text) {
        gfx->render_text(window_id, vk->textbox->font, "Cancel", 
                        cancel_x + 20, cancel_y + 15, vk->text_color, vk->textbox->text_scale);
    }
}

static bool vk_event_handler(AromaEvent *event, void *user_data)
{
    if (!event || !g_vk.visible || !g_vk.keyboard_node)
        return false;
    
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    
    if (!vk_contains_point(x, y))
        return false;
    
    AromaVKWidget *vk = (AromaVKWidget *)g_vk.keyboard_node->node_widget_ptr;
    if (!vk || !vk->textbox)
        return false;
    
    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK: {
            int submit_x, submit_y, submit_w, submit_h;
            int cancel_x, cancel_y, cancel_w, cancel_h;
            vk_get_button_rects(vk, &submit_x, &submit_y, &submit_w, &submit_h,
                               &cancel_x, &cancel_y, &cancel_w, &cancel_h);
            
            if (x >= submit_x && x <= submit_x + submit_w &&
                y >= submit_y && y <= submit_y + submit_h) {
                aroma_textbox_set_focused(g_vk.textbox_node, false);
                return true;
            }
            
            if (x >= cancel_x && x <= cancel_x + cancel_w &&
                y >= cancel_y && y <= cancel_y + cancel_h) {
                aroma_textbox_set_focused(g_vk.textbox_node, false);
                return true;
            }
            
            int key_index = vk_get_key_at_position(x, y);
            if (key_index >= 0) {
                char ch = vk_get_key_char(key_index);
                if (ch != '\0') {
                    textbox_insert_char(vk->textbox, ch);
                    
                    if (g_vk.shift_pressed) {
                        g_vk.shift_pressed = false;
                    }
                    
                    aroma_node_invalidate(g_vk.textbox_node);
                    aroma_node_invalidate(g_vk.keyboard_node);
                    return true;
                }
            }
            
            int special_y = vk->rect.y + vk_get_special_y();
            if (y >= special_y && y <= special_y + AROMA_VK_KEY_HEIGHT) {
                int special_x = vk->rect.x + AROMA_VK_PADDING;
                
                if (x >= special_x && x <= special_x + AROMA_VK_KEY_WIDTH * 2) {
                    g_vk.shift_pressed = !g_vk.shift_pressed;
                    aroma_node_invalidate(g_vk.keyboard_node);
                    return true;
                }
                
                int space_width = g_vk.width - AROMA_VK_PADDING * 2 - AROMA_VK_KEY_WIDTH * 4 - AROMA_VK_KEY_SPACING * 4;
                int space_x = special_x + AROMA_VK_KEY_WIDTH * 2 + AROMA_VK_KEY_SPACING;
                if (x >= space_x && x <= space_x + space_width) {
                    textbox_insert_char(vk->textbox, ' ');
                    aroma_node_invalidate(g_vk.textbox_node);
                    aroma_node_invalidate(g_vk.keyboard_node);
                    return true;
                }
                
                int backspace_x = space_x + space_width + AROMA_VK_KEY_SPACING;
                if (x >= backspace_x && x <= backspace_x + AROMA_VK_KEY_WIDTH * 2) {
                    textbox_backspace(vk->textbox);
                    aroma_node_invalidate(g_vk.textbox_node);
                    aroma_node_invalidate(g_vk.keyboard_node);
                    return true;
                }
            }
            return true;
        }
        
        case EVENT_TYPE_MOUSE_MOVE: {
            int key_index = vk_get_key_at_position(x, y);
            if (key_index != vk->hovered_key) {
                vk->hovered_key = key_index;
                aroma_node_invalidate(g_vk.keyboard_node);
            }
            return true;
        }
        
        case EVENT_TYPE_MOUSE_RELEASE: {
            vk->active_key = -1;
            aroma_node_invalidate(g_vk.keyboard_node);
            return true;
        }
        
        default:
            break;
    }
    
    return true;
}

static AromaNode *vk_create_overlay(AromaNode *textbox_node, AromaTextbox *textbox)
{
    AromaNode *root = aroma_event_get_root();
    if (!root)
        return NULL;
    
    AromaVKWidget *vk_data = (AromaVKWidget *)calloc(1, sizeof(AromaVKWidget));
    if (!vk_data)
        return NULL;
    
    vk_data->rect = (AromaRect){
        .x = g_vk.x,
        .y = g_vk.y,
        .width = g_vk.width,
        .height = g_vk.height
    };
    vk_data->textbox = textbox;
    vk_data->active_key = -1;
    vk_data->hovered_key = -1;
    
    AromaTheme theme = aroma_theme_get_global();
    vk_data->bg_color = theme.colors.background;
    vk_data->key_color = theme.colors.surface;
    vk_data->key_hover_color = theme.colors.primary_light;
    vk_data->key_active_color = theme.colors.primary;
    vk_data->text_color = theme.colors.text_primary;
    vk_data->border_color = theme.colors.border;
    vk_data->preview_bg = theme.colors.background;
    vk_data->preview_text = theme.colors.text_primary;
    vk_data->submit_color = theme.colors.primary;
    vk_data->submit_text = theme.colors.text_primary;
    
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, root, vk_data);
    if (!node) {
        free(vk_data);
        return NULL;
    }
    
    aroma_node_set_draw_cb(node, vk_draw_widget);
    aroma_node_set_z_index(node, 10000);
    
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, vk_event_handler, NULL, 1000);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, vk_event_handler, NULL, 1000);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, vk_event_handler, NULL, 1000);
    
    return node;
}

static void textbox_insert_char(AromaTextbox *tb, char ch)
{
    if (!tb || tb->text_length >= AROMA_TEXTBOX_MAX_LENGTH - 1)
        return;

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

    tb->cursor_pos--;
    
    if (tb->cursor_pos < tb->text_length - 1) {
        memmove(&tb->text[tb->cursor_pos], 
                &tb->text[tb->cursor_pos + 1], 
                tb->text_length - tb->cursor_pos - 1);
    }
    
    tb->text_length--;
    tb->text[tb->text_length] = '\0';
    
    if (tb->scroll_offset > tb->cursor_pos)
        tb->scroll_offset = tb->cursor_pos;
    
    tb->show_cursor = true;
    tb->cursor_blink_time = textbox_now_ms();

    if (tb->on_text_changed)
        tb->on_text_changed(NULL, tb->text, tb->user_data);
}

static void textbox_delete(AromaTextbox *tb)
{
    if (!tb || tb->cursor_pos >= tb->text_length)
        return;

    if (tb->cursor_pos < tb->text_length - 1) {
        memmove(&tb->text[tb->cursor_pos], 
                &tb->text[tb->cursor_pos + 1], 
                tb->text_length - tb->cursor_pos - 1);
    }
    
    tb->text_length--;
    tb->text[tb->text_length] = '\0';
    
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

void aroma_textbox_enable_virtual_keyboard(AromaNode *node, bool enable)
{
    if (!node) return;
    g_vk.enabled = enable;
    
    if (!enable && g_vk.visible && g_vk.textbox_node == node) {
        aroma_textbox_hide_virtual_keyboard(node);
    }
}

bool aroma_textbox_is_virtual_keyboard_enabled(AromaNode *node)
{
    return g_vk.enabled;
}

void aroma_textbox_show_virtual_keyboard(AromaNode *node)
{
    if (!node || !g_vk.enabled) return;
    
    AromaTextbox *data = (AromaTextbox *)node->node_widget_ptr;
    if (!data || !data->is_focused) return;
    
    AromaNode *root = aroma_event_get_root();
    if (root) {
        AromaRect *root_bounds = aroma_node_get_rect(root);
        if (root_bounds) {
            g_vk.window_width = root_bounds->width;
            g_vk.window_height = root_bounds->height;
        }
    }
    
    g_vk.x = 0;
    g_vk.y = 0;
    g_vk.width = g_vk.window_width;
    g_vk.height = g_vk.window_height;
    g_vk.textbox_node = node;
    g_vk.visible = true;
    g_vk.shift_pressed = false;
    g_vk.active_key = -1;
    g_vk.hovered_key = -1;
    
    if (!g_vk.keyboard_node) {
        g_vk.keyboard_node = vk_create_overlay(node, data);
    } else {
        AromaVKWidget *vk = (AromaVKWidget *)g_vk.keyboard_node->node_widget_ptr;
        if (vk) {
            vk->rect.x = g_vk.x;
            vk->rect.y = g_vk.y;
            vk->rect.width = g_vk.width;
            vk->rect.height = g_vk.height;
            vk->textbox = data;
        }
        aroma_node_set_hidden(g_vk.keyboard_node, false);
    }
    
    aroma_node_invalidate(g_vk.keyboard_node);
}

void aroma_textbox_hide_virtual_keyboard(AromaNode *node)
{
    if (g_vk.visible && g_vk.textbox_node == node) {
        g_vk.visible = false;
        g_vk.textbox_node = NULL;
        
        if (g_vk.keyboard_node) {
            aroma_node_set_hidden(g_vk.keyboard_node, true);
            aroma_node_invalidate(g_vk.keyboard_node);
        }
    }
}

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
        
        if (g_vk.enabled) {
            aroma_textbox_show_virtual_keyboard(node);
        }
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
        
        aroma_textbox_hide_virtual_keyboard(node);
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
    
    if (g_vk.textbox_node == node) {
        g_vk.visible = false;
        g_vk.textbox_node = NULL;
        
        if (g_vk.keyboard_node) {
            g_vk.keyboard_node = NULL;
        }
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
        
        if (key == 8 || key == 127 || key == 0xFF08 || key == 0x0008 || key == 0x007F)
        {
            textbox_backspace(tb);
            aroma_node_invalidate(event->target_node);
            return true;
        }

        if (key == 0xFFFF || key == 0xFF9F || key == 0x007F || key == 127)
        {
            textbox_delete(tb);
            aroma_node_invalidate(event->target_node);
            return true;
        }

        if (key == 0xFF51 || key == 0xFF52 || key == 0x0025)
        {
            if (tb->cursor_pos > 0)
            {
                tb->cursor_pos--;
                tb->show_cursor = true;
                tb->cursor_blink_time = textbox_now_ms();
                aroma_node_invalidate(event->target_node);
            }
            return true;
        }

        if (key == 0xFF53 || key == 0xFF54 || key == 0x0027)
        {
            if (tb->cursor_pos < tb->text_length)
            {
                tb->cursor_pos++;
                tb->show_cursor = true;
                tb->cursor_blink_time = textbox_now_ms();
                aroma_node_invalidate(event->target_node);
            }
            return true;
        }

        if (key == 0xFF50 || key == 0x0024)
        {
            tb->cursor_pos = 0;
            tb->scroll_offset = 0;
            tb->show_cursor = true;
            tb->cursor_blink_time = textbox_now_ms();
            aroma_node_invalidate(event->target_node);
            return true;
        }

        if (key == 0xFF57 || key == 0x0023)
        {
            tb->cursor_pos = tb->text_length;
            tb->show_cursor = true;
            tb->cursor_blink_time = textbox_now_ms();
            aroma_node_invalidate(event->target_node);
            return true;
        }

        if (key == 0xFFE5)
        {
            tb->caps_lock_on = !tb->caps_lock_on;
            aroma_node_invalidate(event->target_node);
            return true;
        }

        if (key >= 32 && key <= 126)
        {
            char ch = (char)(key & 0xFF);
            if (tb->caps_lock_on)
                ch = (char)toupper(ch);
            textbox_insert_char(tb, ch);
            aroma_node_invalidate(event->target_node);
            return true;
        }

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