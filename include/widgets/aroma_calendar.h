#ifndef AROMA_CALENDAR_H
#define AROMA_CALENDAR_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaCalendar AromaCalendar;

typedef void (*AromaCalendarSelectCb)(AromaNode *node, int year, int month,
                                      int day, void *user_data);

AromaNode *aroma_calendar_create(AromaNode *parent, int x, int y, int width,
                                 int height);

void aroma_calendar_set_date(AromaNode *cal_node, int year, int month, int day);
void aroma_calendar_get_date(AromaNode *cal_node, int *year, int *month,
                             int *day);
void aroma_calendar_set_selected(AromaNode *cal_node, int year, int month,
                                 int day);
/* Year-grid view (Material year selection): shows 12 years per page with
 * chevron paging. Tapping the header title toggles it at runtime. */
void aroma_calendar_set_year_view(AromaNode *cal_node, bool show_years);
bool aroma_calendar_get_year_view(AromaNode *cal_node);
/* Popup-only widget: the in-tree widget is a compact date field;
 * tapping it opens a modal floating panel (scrim + calendar, day tap confirms).
 * set_popup is a no-op kept for source compatibility. */
void aroma_calendar_set_popup(AromaNode *cal_node, bool popup);
void aroma_calendar_open_popup(AromaNode *cal_node);
void aroma_calendar_close_popup(AromaNode *cal_node);
bool aroma_calendar_is_popup_open(AromaNode *cal_node);
bool aroma_calendar_any_popup_open(void);
void aroma_calendar_close_popups(void);
void aroma_calendar_render_overlays(size_t window_id);
bool aroma_calendar_overlay_hit_test(int x, int y, AromaNode **out_node);
void aroma_calendar_set_on_select(AromaNode *cal_node,
                                  AromaCalendarSelectCb cb, void *user_data);
void aroma_calendar_set_font(AromaNode *cal_node, AromaFont *font);
bool aroma_calendar_setup_events(AromaNode *cal_node,
                                 void (*on_redraw)(void *), void *user_data);
void aroma_calendar_draw(AromaNode *cal_node, size_t window_id);
void aroma_calendar_destroy(AromaNode *cal_node);

int aroma_calendar_days_in_month(int year, int month);
int aroma_calendar_first_weekday(int year, int month);
bool aroma_calendar_is_leap(int year);

#ifdef __cplusplus
}
#endif
#endif
