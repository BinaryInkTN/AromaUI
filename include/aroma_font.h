#ifndef AROMA_FONT_H
#define AROMA_FONT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct AromaFont AromaFont;

/**
 * Create a new font from a font file with specified size in pixels
 * @param font_path Path to font file (TTF, OTF, etc.)
 * @param size_px Font size in pixels
 * @return Pointer to font, NULL on failure
 */
AromaFont* aroma_font_create(const char* font_path, int size_px);

/**
 * Destroy a font and free all resources
 * @param font Font to destroy
 */
void aroma_font_destroy(AromaFont* font);

/**
 * Get the line height for this font
 * @param font Font to query
 * @return Line height in pixels
 */
int aroma_font_get_line_height(AromaFont* font);

/**
 * Get the maximum ascender height for this font
 * @param font Font to query
 * @return Ascender height in pixels
 */
int aroma_font_get_ascender(AromaFont* font);

/**
 * Get the maximum descender height for this font
 * @param font Font to query
 * @return Descender height in pixels
 */
int aroma_font_get_descender(AromaFont* font);

/**
 * Get the internal FT_Face handle (internal use only, internal header only)
 * @param font Font to query
 * @return FT_Face handle or NULL
 */
void* aroma_font_get_face(AromaFont* font);

#endif
