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



#include "widgets/aroma_icon.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_ICON_TEXT_MAX 16
#define AROMA_ICON_PATH_MAX 256

typedef enum {
    AROMA_ICON_MODE_NONE,
    AROMA_ICON_MODE_FONT,
    AROMA_ICON_MODE_IMAGE
} AromaIconMode;

struct AromaIcon {
    AromaRect rect;
    AromaIconMode mode;
    
    
    char icon_text[AROMA_ICON_TEXT_MAX];
    AromaFont* font;
    
    
    char image_path[AROMA_ICON_PATH_MAX];
    unsigned int texture_id;
    bool owns_texture;
    
    uint32_t color;
    float scale;
};


void aroma_icon_draw(AromaNode* icon_node, size_t window_id);

static void __icon_cleanup_texture(AromaIcon* icon) {
    if (icon->mode == AROMA_ICON_MODE_IMAGE && icon->owns_texture && icon->texture_id != 0) {
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->unload_image) {
            gfx->unload_image(icon->texture_id);
        }
        icon->texture_id = 0;
        icon->owns_texture = false;
    }
}

AromaNode* aroma_icon_create(AromaNode* parent, int x, int y, int size) {
    if (!parent) return NULL;
    
    AromaIcon* icon = (AromaIcon*)aroma_widget_alloc(sizeof(AromaIcon));
    if (!icon) return NULL;
    
    memset(icon, 0, sizeof(AromaIcon));
    icon->rect.x = x;
    icon->rect.y = y;
    icon->rect.width = size;
    icon->rect.height = size;
    icon->mode = AROMA_ICON_MODE_NONE;
    icon->color = 0xFF000000; 
    icon->scale = 1.0f;
    
    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, icon);
    if (!node) {
        aroma_widget_free(icon);
        return NULL;
    }
    aroma_node_set_draw_cb(node, (void (*)(AromaNode*, size_t))aroma_icon_draw);
    
    return node;
}

void aroma_icon_set_text(AromaNode* icon_node, const char* icon_text, AromaFont* font) {
    if (!icon_node || !icon_text) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    
    __icon_cleanup_texture(icon);
    
    icon->mode = AROMA_ICON_MODE_FONT;
    strncpy(icon->icon_text, icon_text, AROMA_ICON_TEXT_MAX - 1);
    icon->font = font;
    
    aroma_node_invalidate(icon_node);
}

void aroma_icon_set_image(AromaNode* icon_node, const char* image_path) {
    if (!icon_node || !image_path) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    
    __icon_cleanup_texture(icon);
    
    icon->mode = AROMA_ICON_MODE_IMAGE;
    strncpy(icon->image_path, image_path, AROMA_ICON_PATH_MAX - 1);
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->load_image) {
        icon->texture_id = gfx->load_image(image_path);
        icon->owns_texture = true;
    }
    
    aroma_node_invalidate(icon_node);
}

void aroma_icon_set_texture(AromaNode* icon_node, unsigned int texture_id) {
    if (!icon_node) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    
    __icon_cleanup_texture(icon);
    
    icon->mode = AROMA_ICON_MODE_IMAGE;
    icon->texture_id = texture_id;
    icon->owns_texture = false; 
    icon->image_path[0] = '\0';
    
    aroma_node_invalidate(icon_node);
}

void aroma_icon_set_color(AromaNode* icon_node, uint32_t color) {
    if (!icon_node) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    icon->color = color;
    aroma_node_invalidate(icon_node);
}

void aroma_icon_draw(AromaNode* icon_node, size_t window_id) {
    if (!icon_node) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    if (!icon) return;
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    
    if (aroma_node_is_hidden(icon_node)) return;

    if (icon->mode == AROMA_ICON_MODE_FONT && icon->font && gfx->render_text) {
        
        int ascent = aroma_font_get_ascender(icon->font);
        int line_height = aroma_font_get_line_height(icon->font);
        
        
        int text_x = icon->rect.x + (icon->rect.width / 2) - (aroma_font_get_line_width(icon->font, icon->icon_text) / 2);
        int text_y = icon->rect.y + (icon->rect.height - line_height)/2; 
        
        gfx->render_text(window_id, icon->font, icon->icon_text, text_x, text_y, icon->color, icon->scale);
    } 
    else if (icon->mode == AROMA_ICON_MODE_IMAGE && icon->texture_id != 0 && gfx->draw_image) {
        gfx->draw_image(window_id, icon->rect.x, icon->rect.y, icon->rect.width, icon->rect.height, icon->texture_id);
    }
}

void aroma_icon_destroy(AromaNode* icon_node) {
    if (!icon_node) return;
    AromaIcon* icon = (AromaIcon*)icon_node->node_widget_ptr;
    if (icon) {
        __icon_cleanup_texture(icon);
        aroma_widget_free(icon);
    }
    __destroy_node(icon_node);
}
