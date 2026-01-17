#include "aroma_abi.h"
#include "graphics/aroma_graphics_interface.h"
#include "platforms/aroma_platform_interface.h"
#include <stdatomic.h>
#include <stddef.h>

static _Atomic(AromaGraphicsBackendType) current_graphics_backend = GRAPHICS_BACKEND_GLES3;
static _Atomic(AromaPlatformBackendType) current_platform_backend = PLATFORM_BACKEND_GLPS;

void set_graphics_backend_type(AromaGraphicsBackendType type) {
    atomic_store(&current_graphics_backend, type);
}
    
void set_platform_backend_type(AromaPlatformBackendType type) {
    atomic_store(&current_platform_backend, type);
}

AromaGraphicsInterface* get_graphics_interface(void) {
    AromaGraphicsBackendType backend = atomic_load(&current_graphics_backend);
    switch (backend) {
        case GRAPHICS_BACKEND_GLES3:
            return &aroma_graphics_gles3;
        default:
            return NULL;
    }    
}

AromaPlatformInterface* get_platform_interface(void) {
    AromaPlatformBackendType backend = atomic_load(&current_platform_backend);
    switch (backend) {
        case PLATFORM_BACKEND_GLPS:
            return &aroma_platform_glps;
        default:
            return NULL;
    }    
}

AromaBackendABI aroma_backend_abi = {
    .set_graphics_backend_type = set_graphics_backend_type,
    .set_platform_backend_type = set_platform_backend_type,
    .get_graphics_interface = get_graphics_interface,
    .get_platform_interface = get_platform_interface
};