#ifndef AROMA_CANVAS_H
#define AROMA_CANVAS_H

#include "aroma_common.h"
#include "aroma_node.h"

#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_style.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaWindow AromaWindow;
typedef enum {
    AROMA_CANVAS_DRAW_MODE_IDLE,
    AROMA_CANVAS_DRAW_MODE_RECT,
    AROMA_CANVAS_DRAW_MODE_CIRCLE,
    AROMA_CANVAS_DRAW_MODE_LINE,
    AROMA_CANVAS_DRAW_MODE_TEXT,
    AROMA_CANVAS_DRAW_MODE_CLEAR
} CanvasDrawMode;

typedef struct  {
    int x;
    int y;
    int width;
    int height;
    int border_radius;
    bool is_rect_filled;
    CanvasDrawMode draw_mode;
    uint32_t color;
    char text[256]; 
    AromaFont* text_font;  
} AromaCanvas;

AromaNode* aroma_canvas_create(AromaNode* parent, int x, int y, int width, int height); 
void aroma_canvas_draw_rect(AromaNode* node, int x, int y, int width, int height, int border_radius, bool is_rect_filled, uint32_t color);
void aroma_canvas_draw_circle(AromaNode* node, int center_x, int center_y, int radius, uint32_t color);
void aroma_canvas_draw_line(AromaNode* node, int x1, int y1, int x2, int y2, uint32_t color, int thickness);
void aroma_canvas_draw_text(AromaNode* node, const char* text, int x, int y, uint32_t color, AromaFont* font);
void aroma_canvas_clear(AromaNode* node, uint32_t color);


#ifdef __cplusplus
}
#endif

#endif