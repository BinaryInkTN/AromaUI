#ifndef AROMA_TIMEPICKER_H
#define AROMA_TIMEPICKER_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaTimePicker AromaTimePicker;

typedef void (*AromaTimePickerChangeCb)(AromaNode *node, int hour, int minute,
                                        void *user_data);

AromaNode *aroma_timepicker_create(AromaNode *parent, int x, int y, int width,
                                   int height, int hour, int minute);

void aroma_timepicker_set_time(AromaNode *tp_node, int hour, int minute);
void aroma_timepicker_get_time(AromaNode *tp_node, int *hour, int *minute);
void aroma_timepicker_confirm(AromaNode *tp_node);
void aroma_timepicker_cancel(AromaNode *tp_node);
void aroma_timepicker_set_title(AromaNode *tp_node, const char *title);
void aroma_timepicker_set_24h(AromaNode *tp_node, bool use_24h);
/* Popup-only widget: the in-tree widget is a compact time field; tapping
 * it opens a modal floating panel (scrim + dial + Cancel/OK). */
void aroma_timepicker_open_popup(AromaNode *tp_node);
void aroma_timepicker_close_popup(AromaNode *tp_node);
bool aroma_timepicker_is_popup_open(AromaNode *tp_node);
bool aroma_timepicker_any_popup_open(void);
void aroma_timepicker_close_popups(void);
void aroma_timepicker_render_overlays(size_t window_id);
bool aroma_timepicker_overlay_hit_test(int x, int y, AromaNode **out_node);
void aroma_timepicker_set_on_change(AromaNode *tp_node,
                                    AromaTimePickerChangeCb cb,
                                    void *user_data);
void aroma_timepicker_set_font(AromaNode *tp_node, AromaFont *font);
bool aroma_timepicker_setup_events(AromaNode *tp_node,
                                   void (*on_redraw)(void *), void *user_data);
void aroma_timepicker_draw(AromaNode *tp_node, size_t window_id);
void aroma_timepicker_destroy(AromaNode *tp_node);

#ifdef __cplusplus
}
#endif
#endif
