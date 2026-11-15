#ifndef AROMA_THEME_H
#define AROMA_THEME_H

#include <stdint.h>

#define THEME_BACKGROUND 0xFF0F1115u
#define THEME_SURFACE 0xFF171A21u
#define THEME_SURFACE_RAISED 0xFF1D212Au
#define THEME_TEXT 0xFFE6E8EEu
#define THEME_TEXT_MUTED 0xFF8B93A7u
#define THEME_ACCENT 0xFFF97316u
#define THEME_BORDER 0xFF252B38u

uint32_t theme_surface_for_index(int index);
const char *theme_label_for_index(int index);

#endif
