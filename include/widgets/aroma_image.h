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

#ifndef AROMA_IMAGE_H
#define AROMA_IMAGE_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How an image maps its source pixels onto the widget rectangle.
 *
 * Mirrors iOS contentMode / Android scaleType / CSS object-fit:
 * FILL stretches, FIT letterboxes the whole image, COVER crops to fill.
 */
typedef enum AromaImageScaleMode
{
    AROMA_IMAGE_SCALE_FILL = 0, /**< Stretch to fill (default, may distort). */
    AROMA_IMAGE_SCALE_FIT = 1,  /**< Whole image visible, aspect kept, centered. */
    AROMA_IMAGE_SCALE_COVER = 2 /**< Fills the rect, aspect kept, center-cropped. */
} AromaImageScaleMode;

/**
 * @brief Creates a new image widget
 *
 * @param parent Parent node
 * @param image_path Path to the image file (PNG, JPG, etc.)
 * @param x X position
 * @param y Y position
 * @param width Image width (use 0 for natural size)
 * @param height Image height (use 0 for natural size)
 * @return AromaNode* Image node, or NULL on failure
 */
AromaNode* aroma_image_create(AromaNode* parent, const char* image_path,
                              int x, int y, int width, int height);

/**
 * @brief Creates an image widget from binary data in memory
 *
 * @param parent Parent node
 * @param data Pointer to image data
 * @param data_size Size of image data in bytes
 * @param x X position
 * @param y Y position
 * @param width Image width
 * @param height Image height
 * @return AromaNode* Image node, or NULL on failure
 */
AromaNode* aroma_image_create_from_memory(AromaNode* parent, unsigned char* data,
                                          size_t data_size, int x, int y,
                                          int width, int height);

/**
 * @brief Creates an image widget from an existing OpenGL texture
 *
 * @param parent Parent node
 * @param texture_id OpenGL texture ID
 * @param x X position
 * @param y Y position
 * @param width Image width
 * @param height Image height
 * @param take_ownership If true, the widget will delete the texture when destroyed
 * @return AromaNode* Image node, or NULL on failure
 */
AromaNode* aroma_image_create_from_texture(AromaNode* parent, unsigned int texture_id,
                                           int x, int y, int width, int height,
                                           bool take_ownership);

/**
 * @brief Changes the image source
 *
 * @param image_node Image node
 * @param image_path New image file path
 */
void aroma_image_set_source(AromaNode* image_node, const char* image_path);

/**
 * @brief Changes the image size
 *
 * @param image_node Image node
 * @param width New width
 * @param height New height
 */
void aroma_image_set_size(AromaNode* image_node, int width, int height);

/**
 * @brief Sets the corner radius for rounded image corners.
 *
 * The radius is clamped to [0, min(width, height) / 2]. A value of 0
 * (the default) draws a square image; larger values round the corners
 * with anti-aliased edges (GLES3). Backends without rounded-image
 * support draw the image square.
 *
 * @param image_node Image node
 * @param radius Corner radius in pixels
 */
void aroma_image_set_corner_radius(AromaNode* image_node, float radius);

/**
 * @brief Gets the current corner radius.
 *
 * @param image_node Image node
 * @return Corner radius in pixels, or 0 on invalid node
 */
float aroma_image_get_corner_radius(AromaNode* image_node);

/**
 * @brief Sets how the image scales into its rectangle.
 *
 * FIT centers the whole image with aspect preserved (backends draw a
 * smaller rect, so it works everywhere). COVER fills the rectangle and
 * center-crops the source (needs backend UV support: GLES3/Vulkan;
 * others fall back to FILL). FILL (default) stretches.
 *
 * @param image_node Image node
 * @param mode Scale mode
 */
void aroma_image_set_scale_mode(AromaNode* image_node, AromaImageScaleMode mode);

/**
 * @brief Gets the current scale mode.
 *
 * @param image_node Image node
 * @return Scale mode, or FILL on invalid node
 */
AromaImageScaleMode aroma_image_get_scale_mode(AromaNode* image_node);

/**
 * @brief Changes the image position
 *
 * @param image_node Image node
 * @param x New X position
 * @param y New Y position
 */
void aroma_image_set_position(AromaNode* image_node, int x, int y);

/**
 * @brief Gets the current image size
 *
 * @param image_node Image node
 * @param width Output width
 * @param height Output height
 */
void aroma_image_get_size(AromaNode* image_node, int* width, int* height);

/**
 * @brief Gets the current image position
 *
 * @param image_node Image node
 * @param x Output X position
 * @param y Output Y position
 */
void aroma_image_get_position(AromaNode* image_node, int* x, int* y);

/**
 * @brief Gets the OpenGL texture ID
 *
 * @param image_node Image node
 * @return unsigned int Texture ID, or 0 if not loaded
 */
unsigned int aroma_image_get_texture_id(AromaNode* image_node);

/**
 * @brief Gets the image source path
 *
 * @param image_node Image node
 * @return const char* Image path, or NULL if loaded from memory/texture
 */
const char* aroma_image_get_source(AromaNode* image_node);

/**
 * @brief Draw callback for the image widget
 *
 * @param image_node Image node
 * @param window_id Window ID
 */
void aroma_image_draw(AromaNode* image_node, size_t window_id);

/**
 * @brief Destroys an image widget
 *
 * @param image_node Image node
 */
void aroma_image_destroy(AromaNode* image_node);
void aroma_image_set_on_click(AromaNode* image_node, bool (*on_click)(AromaNode*, void*), void* user_data);
void aroma_image_set_on_hover(AromaNode* image_node, bool (*on_hover)(AromaNode*, void*), void* user_data);
#ifdef __cplusplus
}
#endif
#endif // AROMA_IMAGE_H
