#ifndef AROMA_DATEPICKER_H
#define AROMA_DATEPICKER_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaDatePicker AromaDatePicker;

typedef void (*AromaDatePickerChangeCb)(AromaNode *node, int year, int month,
                                        int day, void *user_data);

AromaNode *aroma_datepicker_create(AromaNode *parent, int x, int y, int width,
                                   int height, int year, int month, int day);

void aroma_datepicker_set_date(AromaNode *dp_node, int year, int month,
                               int day);
void aroma_datepicker_get_date(AromaNode *dp_node, int *year, int *month,
                               int *day);
void aroma_datepicker_confirm(AromaNode *dp_node);
void aroma_datepicker_cancel(AromaNode *dp_node);
void aroma_datepicker_set_title(AromaNode *dp_node, const char *title);
void aroma_datepicker_set_year_view(AromaNode *dp_node, bool show_years);
bool aroma_datepicker_get_year_view(AromaNode *dp_node);
/* Popup-only widget: the in-tree widget is a compact date field;
 * tapping it opens a modal floating panel (scrim + calendar + Cancel/OK).
 * set_popup is a no-op kept for source compatibility. */
void aroma_datepicker_set_popup(AromaNode *dp_node, bool popup);
void aroma_datepicker_open_popup(AromaNode *dp_node);
void aroma_datepicker_close_popup(AromaNode *dp_node);
bool aroma_datepicker_is_popup_open(AromaNode *dp_node);
bool aroma_datepicker_any_popup_open(void);
void aroma_datepicker_close_popups(void);
void aroma_datepicker_render_overlays(size_t window_id);
bool aroma_datepicker_overlay_hit_test(int x, int y, AromaNode **out_node);
void aroma_datepicker_set_on_change(AromaNode *dp_node,
                                    AromaDatePickerChangeCb cb,
                                    void *user_data);
void aroma_datepicker_set_font(AromaNode *dp_node, AromaFont *font);
bool aroma_datepicker_setup_events(AromaNode *dp_node,
                                   void (*on_redraw)(void *), void *user_data);
void aroma_datepicker_draw(AromaNode *dp_node, size_t window_id);
void aroma_datepicker_destroy(AromaNode *dp_node);

#ifdef __cplusplus
}
#endif
#endif
