/**
 * @file aroma_drawlist.h
 * @brief Primitives for buffering drawing commands.
 *
 * The draw list system allows widgets to emit drawing commands that are buffered and
 * executed later during the rendering phase, potentially allowing for batching and optimization.
 */
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

/**
 * @brief Opaque handle to a draw command list.
 */
typedef struct  AromaDrawList AromaDrawList;

/**
 * @brief Represents a single drawing task for a node.
 */
typedef struct  AromaDrawTask {
    AromaNode* node;                                     /**< The UI node associated with this task. */
    void (*draw_cb)(AromaNode* node, size_t window_id); /**< Callback function to execute drawing. */
    int32_t z_index;                                     /**< Z-index for ordering tasks. */
} AromaDrawTask;

/**
 * @brief Types of drawing commands supported by the draw list.
 */
typedef enum AromaDrawCmdType {
    AROMA_DRAW_CMD_CLEAR,        /**< Clear screen/area with color. */
    AROMA_DRAW_CMD_FILL_RECT,    /**< Filled rectangle. */
    AROMA_DRAW_CMD_HOLLOW_RECT,  /**< Outlined rectangle. */
    AROMA_DRAW_CMD_ARC,          /**< Arc or circle segment. */
    AROMA_DRAW_CMD_TEXT,         /**< Text rendering. */
    AROMA_DRAW_CMD_IMAGE,        /**< Image rendering. */
    AROMA_DRAW_CMD_SCISSOR_PUSH, /**< Enable scissor clipping region. */
    AROMA_DRAW_CMD_SCISSOR_POP,  /**< Disable scissor clipping region. */
} AromaDrawCmdType;

/**
 * @brief Create a new empty draw list.
 * @return Pointer to the new draw list.
 */
AromaDrawList* aroma_drawlist_create(void);

/**
 * @brief Destroy a draw list and free its resources.
 * @param list Pointer to the draw list.
 */
void aroma_drawlist_destroy(AromaDrawList* list);

/**
 * @brief Reset the draw list to an empty state.
 * @param list Pointer to the draw list.
 */
void aroma_drawlist_reset(AromaDrawList* list);

/**
 * @brief Mark a draw list as active/current for subsequent commands.
 * @param list Pointer to the draw list.
 */
void aroma_drawlist_begin(AromaDrawList* list);

/**
 * @brief End the current draw list session.
 */
void aroma_drawlist_end(void);

/**
 * @brief Check if there is a currently active draw list.
 * @return true if active, false otherwise.
 */
bool aroma_drawlist_is_active(void);

/**
 * @brief Get the currently active draw list.
 * @return Pointer to the active draw list, or NULL.
 */
AromaDrawList* aroma_drawlist_get_active(void);

/**
 * @brief Add a clear command specifically to the draw list.
 * @param list Target draw list.
 * @param color 32-bit RGBA color.
 */
void aroma_drawlist_cmd_clear(AromaDrawList* list, uint32_t color);

/**
 * @brief Add a filled rectangle command.
 * @param list Target draw list.
 * @param x Top-left X coordinate.
 * @param y Top-left Y coordinate.
 * @param width Width.
 * @param height Height.
 * @param color Color.
 * @param is_rounded True if corners should be rounded.
 * @param corner_radius Radius for rounded corners.
 */
void aroma_drawlist_cmd_fill_rect(AromaDrawList* list, int x, int y, int width, int height,
                                  uint32_t color, bool is_rounded, float corner_radius);

/**
 * @brief Add a hollow (outlined) rectangle command.
 * @param list Target draw list.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param width Width.
 * @param height Height.
 * @param color Border color.
 * @param border_width Border thickness.
 * @param is_rounded True if rounded.
 * @param corner_radius Radius for rounded corners.
 */
void aroma_drawlist_cmd_hollow_rect(AromaDrawList* list, int x, int y, int width, int height,
                                    uint32_t color, int border_width, bool is_rounded, float corner_radius);

/**
 * @brief Add an arc drawing command.
 * @param list Target draw list.
 * @param cx Center X coordinate.
 * @param cy Center Y coordinate.
 * @param radius Radius of the arc.
 * @param start_angle Start angle in degrees.
 * @param end_angle End angle in degrees.
 * @param color Color.
 * @param thickness Line thickness.
 */
void aroma_drawlist_cmd_arc(AromaDrawList* list, int cx, int cy, int radius,
                            float start_angle, float end_angle, uint32_t color, int thickness);

/**
 * @brief Add a text drawing command.
 * @param list Target draw list.
 * @param font Font to use.
 * @param text UTF-8 text string.
 * @param x Baseline X coordinate.
 * @param y Baseline Y coordinate.
 * @param color Text color.
 * @param scale Scaling factor (1.0 = normal size).
 */
void aroma_drawlist_cmd_text(AromaDrawList* list, AromaFont* font, const char* text,
                             int x, int y, uint32_t color, float scale);

/**
 * @brief Add an image drawing command.
 * @param list Target draw list.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param width Display width.
 * @param height Display height.
 * @param texture_id GPU texture ID.
 */
void aroma_drawlist_cmd_image(AromaDrawList* list, int x, int y, int width, int height, unsigned int texture_id);

/**
 * @brief Push a scissor clipping region onto the draw list.
 * @param list Target draw list.
 * @param x Left edge of clip region.
 * @param y Top edge of clip region.
 * @param width Width of clip region.
 * @param height Height of clip region.
 */
void aroma_drawlist_cmd_scissor_push(AromaDrawList* list, int x, int y, int width, int height);

/**
 * @brief Pop (disable) the scissor clipping region.
 * @param list Target draw list.
 */
void aroma_drawlist_cmd_scissor_pop(AromaDrawList* list);

/**
 * @brief Execute (flush) all commands in the list to the backend.
 * @param list Target draw list.
 * @param window_id ID of the window to render to.
 */
void aroma_drawlist_flush(AromaDrawList* list, size_t window_id);

void aroma_drawlist_smart_flush(AromaDrawList* list, size_t window_id, int x, int y, int width, int height);
#ifdef __cplusplus
}
#endif
#endif
