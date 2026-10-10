






#ifndef AROMA_DRAWLIST_H
#define AROMA_DRAWLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct  AromaFont AromaFont;
typedef struct  AromaNode AromaNode;




typedef struct  AromaDrawList AromaDrawList;




typedef struct  AromaDrawTask {
    AromaNode* node;
    void (*draw_cb)(AromaNode* node, size_t window_id);
    int32_t z_index;
} AromaDrawTask;




typedef enum AromaDrawCmdType {
    AROMA_DRAW_CMD_CLEAR,
    AROMA_DRAW_CMD_FILL_RECT,
    AROMA_DRAW_CMD_HOLLOW_RECT,
    AROMA_DRAW_CMD_ARC,
    AROMA_DRAW_CMD_LINE,
    AROMA_DRAW_CMD_TEXT,
    AROMA_DRAW_CMD_IMAGE,
    AROMA_DRAW_CMD_SCISSOR_PUSH,
    AROMA_DRAW_CMD_SCISSOR_POP,
    AROMA_DRAW_CMD_BLUR_BACKDROP,
} AromaDrawCmdType;





AromaDrawList* aroma_drawlist_create(void);





void aroma_drawlist_destroy(AromaDrawList* list);





void aroma_drawlist_reset(AromaDrawList* list);





void aroma_drawlist_begin(AromaDrawList* list);




void aroma_drawlist_end(void);





bool aroma_drawlist_is_active(void);





AromaDrawList* aroma_drawlist_get_active(void);






void aroma_drawlist_cmd_clear(AromaDrawList* list, uint32_t color);












void aroma_drawlist_cmd_fill_rect(AromaDrawList* list, int x, int y, int width, int height,
                                  uint32_t color, bool is_rounded, float corner_radius);













void aroma_drawlist_cmd_hollow_rect(AromaDrawList* list, int x, int y, int width, int height,
                                    uint32_t color, int border_width, bool is_rounded, float corner_radius);












void aroma_drawlist_cmd_arc(AromaDrawList* list, int cx, int cy, int radius,
                            float start_angle, float end_angle, uint32_t color, int thickness);












void aroma_drawlist_cmd_line(AromaDrawList* list, int x0, int y0, int x1, int y1,
                             uint32_t color, float thickness, bool round_cap);











void aroma_drawlist_cmd_text(AromaDrawList* list, AromaFont* font, const char* text,
                             int x, int y, uint32_t color, float scale);











void aroma_drawlist_cmd_image(AromaDrawList* list, int x, int y, int width, int height, unsigned int texture_id, float corner_radius);




















void aroma_drawlist_cmd_image_uv(AromaDrawList* list, int x, int y, int width, int height, unsigned int texture_id, float corner_radius,
                                 float u0, float v0, float u1, float v1);









void aroma_drawlist_cmd_scissor_push(AromaDrawList* list, int x, int y, int width, int height);





void aroma_drawlist_cmd_scissor_pop(AromaDrawList* list);
















void aroma_drawlist_cmd_blur_backdrop(AromaDrawList* list, int x, int y,
                                      int width, int height, float radius,
                                      float corner_radius);






void aroma_drawlist_flush(AromaDrawList* list, size_t window_id);

void aroma_drawlist_smart_flush(AromaDrawList* list, size_t window_id, int x, int y, int width, int height);
#ifdef __cplusplus
}
#endif
#endif
