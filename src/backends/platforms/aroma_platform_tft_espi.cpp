#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_logger.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

static TFT_eSPI*     g_tft        = nullptr;
static TFT_eSprite*  g_sprite     = nullptr;
static bool          g_use_sprite = false; 
static int           g_width      = 0;
static int           g_height     = 0;

#define USING_SPRITE() (g_use_sprite && g_sprite)

static void (*g_update_callback)(size_t, void*) = nullptr;
static void* g_callback_data = nullptr;

static TFT_eSprite* get_target_sprite() {
    return g_sprite;  

}

void tft_enable_double_buffer(bool enable) {
    if (!g_tft) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    if (enable && !g_use_sprite) {
        g_sprite = new TFT_eSprite(g_tft);
        if (!g_sprite || !g_sprite->createSprite(g_width, g_height)) {
            LOG_ERROR("Failed to create sprite for double buffering");
            delete g_sprite;
            g_sprite = nullptr;
            return;
        }

        g_sprite->setSwapBytes(true);
        g_use_sprite = true;

        if (gfx->graphics_set_sprite_mode) {
            gfx->graphics_set_sprite_mode(true, g_sprite);
        }
        LOG_INFO("Double buffering enabled (sprite)");
    } 
    else if (!enable && g_use_sprite) {
        delete g_sprite;
        g_sprite = nullptr;
        g_use_sprite = false;
        if (gfx->graphics_set_sprite_mode) {
            gfx->graphics_set_sprite_mode(false, nullptr);
        }
        LOG_INFO("Double buffering disabled");
    }
}

int initialize(void) {
    LOG_INFO("Initializing TFT platform backend");

    aroma_backend_abi.set_platform_backend_type(PLATFORM_BACKEND_TFT_ESPI);

    g_tft = new TFT_eSPI();
    if (!g_tft) return 0;

    g_tft->init();
    g_tft->setRotation(1);

    g_width = g_tft->width();
    g_height = g_tft->height();

#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#endif

    tft_enable_double_buffer(true);

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return 0;

    if (gfx->graphics_set_tft_context) {
        gfx->graphics_set_tft_context((void*)g_tft);
    }
    if (gfx->setup_shared_window_resources) {
        if (!gfx->setup_shared_window_resources()) return 0;
    }

    LOG_INFO("TFT backend initialized: %dx%d", g_width, g_height);
    return 1;
}

void* get_tft_context(void) {
    return (void*)g_tft;
}

size_t create_window(const char* title, int x, int y, int w, int h) {
    (void)title; (void)x; (void)y; (void)w; (void)h;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->setup_separate_window_resources) {
        gfx->setup_separate_window_resources(0);
    }

    return 0; 

}

void make_context_current(size_t window_id) {
    (void)window_id;
}

void set_window_update_callback(void (*callback)(size_t, void*), void* data) {
    g_update_callback = callback;
    g_callback_data = data;
}

void get_window_size(size_t window_id, int* w, int* h) {
    if (window_id != 0) { if(w)*w=0; if(h)*h=0; return; }
    if(w) *w = g_width; if(h) *h = g_height;
}

void request_window_update(size_t window_id) {
    if (window_id != 0) return;
    if (g_update_callback) g_update_callback(window_id, g_callback_data);
}

bool run_event_loop(void) {
    return false; 

}

void swap_buffers(size_t window_id) {
    if (window_id != 0) return;
    if (USING_SPRITE() && g_tft) g_sprite->pushSprite(0, 0);
}

static void shutdown(void) {
    tft_enable_double_buffer(false);

    g_update_callback = nullptr;
    g_callback_data = nullptr;

    if (g_tft) {
        g_tft->fillScreen(TFT_BLACK);
#ifdef TFT_BL
        digitalWrite(TFT_BL, LOW);
#endif
        delete g_tft;
        g_tft = nullptr;
    }
}

AromaPlatformInterface aroma_platform_tft = {
    .initialize                  = initialize,
    .shutdown                    = shutdown,
    .create_window               = create_window,
    .make_context_current        = make_context_current,
    .set_window_update_callback  = set_window_update_callback,
    .get_window_size             = get_window_size,
    .request_window_update       = request_window_update,
    .run_event_loop              = run_event_loop,
    .swap_buffers                = swap_buffers,
    .get_tft_context             = get_tft_context,
};

#ifdef __cplusplus
}
#endif

