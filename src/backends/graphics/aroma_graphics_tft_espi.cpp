#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_logger.h"

#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RGB888_TO_565(c) \
    ((((c) & 0xF80000) >> 8) | (((c) & 0xFC00) >> 5) | (((c) & 0xFF) >> 3))

static TFT_eSPI*    g_tft        = nullptr;
static TFT_eSprite* g_sprite     = nullptr;
static bool         g_use_sprite = false;
static int          g_width      = 0;
static int          g_height     = 0;

typedef struct {
    int x, y, w, h;
    bool enabled;
} ClipRect;
#define MAX_IMAGES 8

typedef struct {
    uint16_t* data;     
    unsigned long len;  
} ImageSlot;

static ImageSlot g_images[MAX_IMAGES] = {0};
static ClipRect g_clip = {0};

#define USING_SPRITE() (g_use_sprite && g_sprite)

static int find_free_slot() {
    for (int i = 0; i < MAX_IMAGES; i++) {
        if (g_images[i].data == NULL) return i;
    }
    return -1; 
}

void graphics_set_clip(int x, int y, int w, int h) {
    g_clip.x = x;
    g_clip.y = y;
    g_clip.w = w;
    g_clip.h = h;
    g_clip.enabled = true;
}

void graphics_clear_clip(void) {
    g_clip.enabled = false;
}

void graphics_set_tft_context(void* tft) {
    if (!tft) return;
    g_tft = (TFT_eSPI*)tft;
    g_width  = g_tft->width();
    g_height = g_tft->height();
}

void graphics_set_sprite_mode(bool enable, void* sprite) {
    g_use_sprite = enable;
    g_sprite     = (TFT_eSprite*)sprite;
}

int setup_shared_window_resources(void) {
    return g_tft != nullptr;
}

int setup_separate_window_resources(size_t window_id) {
    (void)window_id;
    return 1;
}

void clear(size_t window_id, uint32_t color) {
    if (window_id != 0 || !g_tft) return;
    uint16_t c = RGB888_TO_565(color);
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if(platform && platform->set_clear_color) {
        platform->set_clear_color(c);
    }
   
}

void fill_rectangle(size_t window_id, int x, int y, int w, int h,
                    uint32_t color, bool round, float radius) {
    if (window_id != 0 || !g_tft) return;
    uint16_t c = RGB888_TO_565(color);
    int r = (int)radius;
                        
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if(platform && platform->tft_mark_tiles_dirty) {
        platform->tft_mark_tiles_dirty(y, h);
    }
       


    if (USING_SPRITE()) {

            int sx = x - g_clip.x;
            int sy = y - g_clip.y;

             round && r > 0 ? g_sprite->fillRoundRect(sx, sy, w, h, r, c)
                       : g_sprite->fillRect(sx, sy, w, h, c);
    } else {
        round && r > 0 ? g_tft->fillRoundRect(x, y, w, h, r, c)
                       : g_tft->fillRect(x, y, w, h, c);
    }
}

void draw_hollow_rectangle(size_t window_id,
                           int x, int y,
                           int width, int height,
                           uint32_t color,
                           int border_width,
                           bool rounded,
                           float cornerRadius) {
    if (window_id != 0 || !g_tft) return;
    
    uint16_t c = RGB888_TO_565(color);
    int r = (int)cornerRadius;

    for (int i = 0; i < border_width; i++) {
        if (USING_SPRITE()) {
                

            int sx = x - g_clip.x;
            int sy = y - g_clip.y;


            rounded && r > 0 ? g_sprite->drawRoundRect(sx+i, sy+i, width-2*i, height-2*i, r, c)
                              : g_sprite->drawRect(sx+i, sy+i, width-2*i, height-2*i, c);
        } else {
            rounded && r > 0 ? g_tft->drawRoundRect(x+i, y+i, width-2*i, height-2*i, r, c)
                              : g_tft->drawRect(x+i, y+i, width-2*i, height-2*i, c);
        }
    }
}

void draw_rectangle(size_t window_id, int x, int y, int w, int h) {
    draw_hollow_rectangle(window_id, x, y, w, h, 0xFFFFFF, 1, false, 0);
}

void draw_arc(size_t window_id, int cx, int cy, int r,
              float a0, float a1, uint32_t color, int thickness) {
    if (window_id != 0 || !g_tft) return;

    uint16_t c = RGB888_TO_565(color);
    int s = (int)(a0 * 180.0f / M_PI);
    int e = (int)(a1 * 180.0f / M_PI);

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if(platform && platform->tft_mark_tiles_dirty)
        platform->tft_mark_tiles_dirty(cy, r*2); //TODO: better estimate


    if (USING_SPRITE()) {
     

                        int sx = cx - g_clip.x;
                        int sy = cy - g_clip.y;

        g_sprite->drawArc(sx, sy, r, r, s, e, c, thickness);
    }
    else {
        g_tft->drawArc(cx, cy, r, r, s, e, c, thickness);
    }
}
void render_text(size_t window_id, AromaFont* font,
                 const char* text, int x, int y,
                 uint32_t color, float scale)
{
    if (window_id != 0 || !g_tft || !text) return;

    uint16_t c = RGB888_TO_565(color);
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();

    int ascender    = 14;
    int line_height = 18;

    if (platform && platform->tft_mark_tiles_dirty) {
        platform->tft_mark_tiles_dirty(
            y,
            line_height
        );
    }

    int draw_x = x;
    int draw_y = y + ascender;

    if (USING_SPRITE()) {
        int sx = draw_x - g_clip.x;
        int sy = draw_y - g_clip.y;

        g_sprite->setFreeFont(&FreeSans12pt7b);
        g_sprite->setTextSize(1);
        g_sprite->setTextColor(c, TFT_BLACK);
        g_sprite->setTextWrap(false);
        g_sprite->setCursor(sx, sy);
        g_sprite->print(text);
    } else {
        g_tft->setFreeFont(&FreeSans12pt7b);
        g_tft->setTextSize(1);
        g_tft->setTextColor(c);
        g_tft->setTextWrap(false);
        g_tft->setCursor(draw_x, draw_y);
        g_tft->print(text);
    }

    (void)font;
    (void)scale;
}

float measure_text(size_t window_id, AromaFont* font,
                   const char* text, float scale) {
    (void)window_id;
    (void)font;

    if (!text || !g_tft) return 0.0f;

    uint8_t size = (uint8_t)fmaxf(scale, 1.0f);

    g_tft->setFreeFont(&FreeSans12pt7b);
    g_tft->setTextSize(size);

    int w = g_tft->textWidth(text);
    return (float)w;
}

unsigned int load_image(const char* image_path) { (void)image_path; return 0; }
unsigned int load_image_from_memory(const uint16_t* data, unsigned long len) {
    int slot = find_free_slot();
    if (slot < 0) return 0; 

    g_images[slot].data = (uint16_t*)data;
    g_images[slot].len = len;  
    return slot + 1; 
}
void unload_image(unsigned int texture_id) {
    if (texture_id == 0 || texture_id > MAX_IMAGES) return;
    int slot = texture_id - 1;
    g_images[slot].data = NULL;
    g_images[slot].len = 0;
}
void draw_image(size_t window_id, int x, int y,
                int width, int height, unsigned int texture_id) {
    (void)window_id;

    if (texture_id == 0 || texture_id > MAX_IMAGES) return;
    int slot = texture_id - 1;
    if (!g_images[slot].data) return;

    int img_pixels = g_images[slot].len / 2; 
    int src_width = width;
    int src_height = height;

    if (USING_SPRITE()) {
        int sx = x - g_clip.x;
        int sy = y - g_clip.y;
        if (g_sprite) {
            g_sprite->pushImage(sx, sy, width, height, g_images[slot].data);
        }
    } else {
        g_tft->pushImage(x, y, width, height, g_images[slot].data);
    }
}
void shutdown(void) {
    g_use_sprite = false;
    if (g_sprite) { g_sprite->deleteSprite(); delete g_sprite; g_sprite = nullptr; }
    g_tft = nullptr;
}

AromaGraphicsInterface aroma_graphics_tft = {
    .setup_shared_window_resources   = setup_shared_window_resources,
    .setup_separate_window_resources = setup_separate_window_resources,
    .shutdown                        = shutdown,
    .clear                           = clear,
    .draw_rectangle                  = draw_rectangle,
    .fill_rectangle                  = fill_rectangle,
    .draw_hollow_rectangle           = draw_hollow_rectangle,
    .draw_arc                        = draw_arc,
    .render_text                     = render_text,
    .measure_text                    = measure_text,
    .unload_image                    = unload_image,
    .load_image                      = load_image,
    .load_image_from_memory          = load_image_from_memory,
    .draw_image                      = draw_image,
    .graphics_set_tft_context        = graphics_set_tft_context,
    .graphics_set_sprite_mode        = graphics_set_sprite_mode,
    .graphics_set_clip               = graphics_set_clip,
    .graphics_clear_clip             = graphics_clear_clip
};

#ifdef __cplusplus
}
#endif

