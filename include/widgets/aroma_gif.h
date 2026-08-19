#ifndef AROMA_GIF_H
#define AROMA_GIF_H

#include "aroma_common.h"
#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

AromaNode* aroma_gif_create(AromaNode* parent, const char* gif_path,
                            int x, int y, int width, int height);

AromaNode* aroma_gif_create_from_memory(AromaNode* parent, unsigned char* data, size_t data_size,
                                        int x, int y, int width, int height);

void aroma_gif_play(AromaNode* gif_node);
void aroma_gif_pause(AromaNode* gif_node);

#ifdef __cplusplus
}
#endif

#endif // AROMA_GIF_H
