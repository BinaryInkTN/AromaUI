#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#define IOS_COLOR_LABEL 0xFF000000
#define IOS_COLOR_SECONDARY_LABEL 0xFF3C3C43
#define IOS_COLOR_TERTIARY_LABEL 0xFF48484A
#define IOS_COLOR_BLUE 0xFF007AFF
#define IOS_COLOR_GREEN 0xFF34C759
#define IOS_COLOR_RED 0xFFFF3B30
#define IOS_COLOR_ORANGE 0xFFFF9500
#define IOS_COLOR_PURPLE 0xFFAF52DE
#define IOS_COLOR_GRAY 0xFF8E8E93
#define IOS_COLOR_TINT 0xFF007AFF

#include <stdbool.h>

void init_theme(void);
void apply_theme(bool dark_mode);
void toggle_theme(void);

#endif
