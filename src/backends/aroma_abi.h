#ifndef AROMA_ABI_H
#define AROMA_ABI_H
#include "graphics/aroma_graphics_interface.h"
#include "platforms/aroma_platform_interface.h"

typedef struct AromaBackendABI {
    void (*set_graphics_backend_type)(void);
    void (*set_platform_backend_type)(void);
    AromaGraphicsInterface* (*get_graphics_interface)(void);
    AromaPlatformInterface* (*get_platform_interface)(void);
} AromaBackendABI;



#endif