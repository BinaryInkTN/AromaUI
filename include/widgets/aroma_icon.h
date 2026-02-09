#ifndef AROMA_ICON_H
#define AROMA_ICON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaIcon AromaIcon;

/**
 * @brief Create a new Icon widget.
 * @param parent Parent node.
 * @param x X position.
 * @param y Y position.
 * @param size Icon size in pixels.
 * @return New AromaNode representing the icon.
 */
AromaNode* aroma_icon_create(AromaNode* parent, int x, int y, int size);

/**
 * @brief Set the icon to a font character.
 * @param icon_node The icon node.
 * @param icon_text The icon character/text (e.g. from a Material Icons font).
 * @param font The font to use.
 */
void aroma_icon_set_text(AromaNode* icon_node, const char* icon_text, AromaFont* font);

/**
 * @brief Set the icon to an image file.
 * @param icon_node The icon node.
 * @param image_path Path to the image file (generic image).
 */
void aroma_icon_set_image(AromaNode* icon_node, const char* image_path);

/**
 * @brief Set the icon to a raw texture ID.
 * @param icon_node The icon node.
 * @param texture_id OpenGL texture ID.
 */
void aroma_icon_set_texture(AromaNode* icon_node, unsigned int texture_id);

/**
 * @brief Set the color of the icon.
 * Applies to font-based icons. (Image tinting depends on backend support).
 * @param icon_node The icon node.
 * @param color Color value (ARGB or similar).
 */
void aroma_icon_set_color(AromaNode* icon_node, uint32_t color);

/**
 * @brief Destroy the icon widget.
 * @param icon_node The icon node to destroy.
 */
void aroma_icon_destroy(AromaNode* icon_node);

#ifdef __cplusplus
}
#endif

#endif // AROMA_ICON_H
