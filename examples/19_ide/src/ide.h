#ifndef IDE_H
#define IDE_H

#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_incense_loader.h"

#define IDE_UI_FONT_COUNT 13
#define IDE_ICON_FONT_COUNT 10
#define IDE_MONO_FONT_COUNT 7

                                                                         
#define IDE_WINDOW_OPACITY 1.0f

typedef struct IdeAssets
{
    AromaFont *ui[IDE_UI_FONT_COUNT];                           
    AromaFont *ui_mono[IDE_UI_FONT_COUNT];                 
    AromaFont *ui_serif[IDE_UI_FONT_COUNT];            
    AromaFont *icons[IDE_ICON_FONT_COUNT];
    AromaFont *mono[IDE_MONO_FONT_COUNT];
    AromaFont *editor;
} IdeAssets;

extern const int ide_ui_font_sizes[IDE_UI_FONT_COUNT];
extern const int ide_icon_font_sizes[IDE_ICON_FONT_COUNT];
extern const int ide_mono_font_sizes[IDE_MONO_FONT_COUNT];

void ide_after_mount(IncenseRegistry *registry, IdeAssets *assets);
void ide_tick(void);
void ide_shutdown(void);

                 
void ide_home_init(IncenseRegistry *registry, IdeAssets *assets);
void ide_home_draw(void);
bool ide_home_on_click(AromaEvent *event, void *user_data);
bool ide_home_active(void);
const char *ide_home_project_path(void);
void ide_home_fail(const char *message);
                                                                       
void ide_home_set_recent_project(const char *path);
int ide_home_pending_dialog(void);
void ide_home_pending_dialog_clear(void);
void ide_home_open_pending_dialog(void);

#endif