/**
 * @file aroma_font.h
 * @brief Font loading, metrics, and management.
 *
 * Provides functions to load fonts from files or memory and query their metrics
 * (text width, line height, etc.).
 */
#ifndef AROMA_FONT_H
#define AROMA_FONT_H

#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaFont AromaFont;

/**
 * @brief Load a font from a file on disk.
 * @param font_path Path to the font file (TTF, OTF, etc.).
 * @param size_px Requested size in pixels.
 * @return Pointer to the loaded font, or NULL on error.
 */
AromaFont* aroma_font_create(const char* font_path, int size_px);

/**
 * @brief Load a font from a memory buffer.
 * @param data Pointer to the font data.
 * @param data_len Length of the data in bytes.
 * @param size_px Requested size in pixels.
 * @return Pointer to the loaded font, or NULL on error.
 */
AromaFont* aroma_font_create_from_memory(const unsigned char* data, unsigned int data_len, int size_px);

/**
 * @brief Destroy a font and release its resources.
 * @param font Pointer to the font.
 */
void aroma_font_destroy(AromaFont* font);

/**
 * @brief Get the line height of the font.
 * @param font Pointer to the font.
 * @return Line height in pixels.
 */
int aroma_font_get_line_height(AromaFont* font);

/**
 * @brief Get the font's ascender value.
 * @param font Pointer to the font.
 * @return Ascender height in pixels.
 */
int aroma_font_get_ascender(AromaFont* font);

/**
 * @brief Get the font's descender value.
 * @param font Pointer to the font.
 * @return Descender height in pixels.
 */
int aroma_font_get_descender(AromaFont* font);

/**
 * @brief Get the underlying implementation-specific font face object (e.g. FT_Face).
 * @param font Pointer to the font.
 * @return Pointer to the raw face object, or NULL.
 */
void* aroma_font_get_face(AromaFont* font);

/**
 * @brief Calculate the width of a text string in pixels using this font.
 * @param font Pointer to the font.
 * @param text The text string to measure.
 * @return Width in pixels.
 */
int aroma_font_get_line_width(AromaFont* font, const char* text);
#ifdef __cplusplus
}
#endif
#endif

