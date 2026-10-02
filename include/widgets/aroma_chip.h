#ifndef AROMA_CHIP_H
#define AROMA_CHIP_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        CHIP_TYPE_ASSIST,
        CHIP_TYPE_FILTER,
        CHIP_TYPE_INPUT,
        CHIP_TYPE_SUGGESTION
    } AromaChipType;

    typedef struct AromaChip AromaChip;

    AromaNode *aroma_chip_create(AromaNode *parent, int x, int y, const char *label, AromaChipType type);

    void aroma_chip_set_callback(AromaNode *chip_node, void (*callback)(void *user_data), void *user_data);

    void aroma_chip_set_selected(AromaNode *chip_node, bool selected);

    void aroma_chip_set_font(AromaNode *chip_node, AromaFont *font);

    void aroma_chip_set_icon(AromaNode *chip_node, const char *icon, AromaFont *icon_font);

    void aroma_chip_set_text(AromaNode *chip_node, const char *text);

    void aroma_chip_draw(AromaNode *chip_node, size_t window_id);

    void aroma_chip_destroy(AromaNode *chip_node);
#ifdef __cplusplus
}
#endif
#endif
