#include "widgets/aroma_gif.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "backends/graphics/utils/stb_image.h"
#include <string.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

typedef struct   AromaGif {
    AromaRect rect;
    uint64_t last_frame_time;
    unsigned int* textures;
    int* delays;
    AromaTimer* timer;
    AromaNode* node;
    int num_frames;
    int current_frame;
    bool is_playing;
} AromaGif;

static void aroma_gif_timer_cb(void* user_data) {
    AromaGif* gif = (AromaGif*)user_data;
    if (!gif || !gif->is_playing || gif->num_frames <= 1) return;

    uint64_t now = aroma_time_now_ms();
    int current_delay = gif->delays[gif->current_frame];
    // STB delays are often in centiseconds (1/100 sec) if not otherwise converted
    int delay_ms = current_delay;

    if (now - gif->last_frame_time >= delay_ms) {
        gif->current_frame = (gif->current_frame + 1) % gif->num_frames;
        gif->last_frame_time = now;
        if (gif->node) {
            aroma_node_invalidate(gif->node);
        }
    }
}

static void __gif_destroy(AromaGif* gif) {
    if (gif->timer) {
        aroma_timer_cancel(gif->timer);
        gif->timer = NULL;
    }
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->unload_image && gif->textures) {
        for (int i = 0; i < gif->num_frames; i++) {
            if (gif->textures[i] != 0) {
                gfx->unload_image(gif->textures[i]);
            }
        }
    }
    
    if (gif->textures) free(gif->textures);
    if (gif->delays) stbi_image_free(gif->delays);
}

void aroma_gif_draw(AromaNode* gif_node, size_t window_id) {
    if (!gif_node || !gif_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(gif_node)) return;

    AromaGif* gif = (AromaGif*)gif_node->node_widget_ptr;
    if (gif->num_frames == 0 || !gif->textures) return;

    unsigned int tex = gif->textures[gif->current_frame];
    if (tex == 0) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->draw_image) {
        gfx->draw_image(window_id, gif->rect.x, gif->rect.y, gif->rect.width, gif->rect.height, tex);
    }
}

AromaNode* aroma_gif_create_from_memory(AromaNode* parent, unsigned char* data, size_t data_size, int x, int y, int width, int height) {
    if (!parent || !data || data_size == 0) return NULL;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->load_image_from_rgba) {
        LOG_ERROR("Graphics interface does not support RGB frame uploading");
        return NULL;
    }

    int *delays = NULL;
    int w, h, frames, comp;
    unsigned char* raw_data = stbi_load_gif_from_memory(data, (int)data_size, &delays, &w, &h, &frames, &comp, 4);
    
    if (!raw_data || frames <= 0) {
        LOG_ERROR("Failed to load GIF from memory");
        if (raw_data) stbi_image_free(raw_data);
        return NULL;
    }

    AromaGif* gif = (AromaGif*)aroma_widget_alloc(sizeof(AromaGif));
    if (!gif) {
        stbi_image_free(raw_data);
        stbi_image_free(delays);
        return NULL;
    }
    memset(gif, 0, sizeof(AromaGif));

    gif->rect.x = x;
    gif->rect.y = y;
    gif->rect.width = width > 0 ? width : w;
    gif->rect.height = height > 0 ? height : h;
    gif->num_frames = frames;
    gif->current_frame = 0;
    gif->is_playing = true;
    gif->textures = (unsigned int*)malloc(frames * sizeof(unsigned int));
    gif->delays = delays;

    for (int i = 0; i < frames; i++) {
        // raw_data is arranged sequentially as frame0, frame1, etc.
        unsigned char* frame_data = raw_data + (i * w * h * 4);
        gif->textures[i] = gfx->load_image_from_rgba(frame_data, w, h);
    }

    stbi_image_free(raw_data);

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, gif);
    if (!node) {
        __gif_destroy(gif);
        aroma_widget_free(gif);
        return NULL;
    }

    gif->node = node;
    aroma_node_set_draw_cb(node, aroma_gif_draw);

    if (frames > 1) {
        gif->last_frame_time = aroma_time_now_ms();
        gif->timer = aroma_timer_create(30, true, aroma_gif_timer_cb, gif);
    }

    return node;
}

AromaNode* aroma_gif_create(AromaNode* parent, const char* gif_path, int x, int y, int width, int height) {
    if (!parent || !gif_path) return NULL;

#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif


    FILE* f = fopen(gif_path, "rb");
    if (!f) {
        LOG_ERROR("Could not open GIF file: %s", gif_path);
        return NULL;
    }


    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* data = (unsigned char*)malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(data, 1, size, f);
    fclose(f);

    if (read_bytes != (size_t)size) {
        free(data);
        return NULL;
    }

    AromaNode* node = aroma_gif_create_from_memory(parent, data, size, x, y, width, height);
    free(data);
    return node;
}

void aroma_gif_play(AromaNode* gif_node) {
    if (!gif_node || !gif_node->node_widget_ptr) return;
    AromaGif* gif = (AromaGif*)gif_node->node_widget_ptr;
    gif->is_playing = true;
    gif->last_frame_time = aroma_time_now_ms();
}

void aroma_gif_pause(AromaNode* gif_node) {
    if (!gif_node || !gif_node->node_widget_ptr) return;
    AromaGif* gif = (AromaGif*)gif_node->node_widget_ptr;
    gif->is_playing = false;
}
