#ifndef AROMA_FONT_H
#define AROMA_FONT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct AromaFont AromaFont;


AromaFont* aroma_font_create(const char* font_path, int size_px);

AromaFont* aroma_font_create_from_memory(const unsigned char* data, unsigned int data_len, int size_px);

void aroma_font_destroy(AromaFont* font);

int aroma_font_get_line_height(AromaFont* font);

int aroma_font_get_ascender(AromaFont* font);

int aroma_font_get_descender(AromaFont* font);

void* aroma_font_get_face(AromaFont* font);

#endif

