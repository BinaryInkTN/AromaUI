#include "theme.h"

uint32_t theme_surface_for_index(int index)
{
    return (index & 1) == 0 ? THEME_SURFACE : THEME_SURFACE_RAISED;
}

const char *theme_label_for_index(int index)
{
    switch (index % 3)
    {
    case 0:
        return "Focused";
    case 1:
        return "Pinned";
    default:
        return "Recent";
    }
}
