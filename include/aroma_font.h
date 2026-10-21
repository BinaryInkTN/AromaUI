

#ifndef AROMA_FONT_H
#define AROMA_FONT_H

#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct  AromaFont AromaFont;


AromaFont* aroma_font_create(const char* font_path, int size_px);


AromaFont* aroma_font_create_from_memory(const unsigned char* data, unsigned int data_len, int size_px);


void aroma_font_destroy(AromaFont* font);


int aroma_font_get_line_height(AromaFont* font);


int aroma_font_get_ascender(AromaFont* font);


int aroma_font_get_descender(AromaFont* font);


void* aroma_font_get_face(AromaFont* font);


int aroma_font_get_line_width(AromaFont* font, const char* text);

int aroma_font_get_px_size(AromaFont* font);

/* Raster density: device px per design px.
 *
 * Web backends lay out in fixed design units (the markup Window size)
 * but render into a backing store sized to the actual displayed CSS box
 * times devicePixelRatio. aroma_font_set_raster_scale(k) re-rasterizes
 * every live face at round(design_px * k) so glyphs land 1:1 with device
 * pixels; all metric getters keep returning design units, so widget
 * layout math is unaffected. Bumping the scale also bumps a generation
 * counter (see aroma_font_raster_generation) so cached glyph atlases
 * can be rebuilt. Desktop backends leave the scale at 1.0.
 */
void aroma_font_set_raster_scale(float scale);
unsigned aroma_font_raster_generation(void);
/* Device px per design px right now. Widgets that rasterize their own
 * textures read this so the raster comes out exactly device-resolution,
 * matching what the font atlas does (see aroma_font_set_raster_scale). */
float aroma_font_raster_scale(void);
/* design_px / raster_px for one font: multiply raster-space glyph
 * metrics with this to get design units. */
float aroma_font_design_scale(AromaFont* font);


void aroma_font_lock(void);
void aroma_font_unlock(void);
#ifdef __cplusplus
}
#endif
#endif

