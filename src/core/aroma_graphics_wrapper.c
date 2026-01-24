/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"

void aroma_gles3_load_font_for_window(size_t window_id, AromaFont* font);

void aroma_graphics_clear(size_t window_id, uint32_t color)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->clear) {
        gfx->clear(window_id, color);
    }
}

void aroma_graphics_fill_rectangle(size_t window_id, int x, int y, int width, int height,
                                   uint32_t color, bool filled, float rotation)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->fill_rectangle) {
        gfx->fill_rectangle(window_id, x, y, width, height, color, filled, rotation);
    }
}

void aroma_graphics_render_text(size_t window_id, AromaFont* font, const char* text,
                                int x, int y, uint32_t color, float scale)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->render_text && font && text) {
        gfx->render_text(window_id, font, text, x, y, color, scale);
    }
}

void aroma_graphics_load_font_for_window(size_t window_id, AromaFont* font)
{

    aroma_gles3_load_font_for_window(window_id, font);
}

void aroma_graphics_swap_buffers(size_t window_id)
{
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->swap_buffers) {
        platform->swap_buffers(window_id);
    }
}

void aroma_platform_initialize(void)
{
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->initialize) {
        platform->initialize();
    }
}

void aroma_platform_request_window_update(size_t window_id)
{
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->request_window_update) {
        platform->request_window_update(window_id);
    }
}

void aroma_platform_set_window_update_callback(void (*callback)(size_t window_id, void* data), void* user_data)
{
    /*    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->set_window_update_callback) {
        platform->set_window_update_callback(callback, user_data);
    }*/

}

bool aroma_platform_run_event_loop(void)
{
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->run_event_loop) {
        return platform->run_event_loop();
    }
    return false;
}
