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
#ifndef ESP32

#ifndef AROMA_VULKAN_TEXT_H
#define AROMA_VULKAN_TEXT_H

#include <vulkan/vulkan.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>
#include <stdbool.h>

#define VK_TEXT_MAX_GLYPHS 512

typedef struct VkTextureHandle VkTextureHandle;

typedef struct {
    uint32_t codepoint;
    VkImage       image;
    VkDeviceMemory memory;
    VkImageView   imageView;
    VkSampler     sampler;
    VkDescriptorSet descriptorSet;
    int  width;
    int  height;
    int  bearingX;
    int  bearingY;
    int  advance;
    bool valid;
} VulkanGlyph;

typedef struct {
    VulkanGlyph glyphs[VK_TEXT_MAX_GLYPHS];
    int         glyphCount;
    int         fontHeight;
    FT_Face     face;
} VulkanTextRenderer;

int  vulkan_text_renderer_init(VulkanTextRenderer* renderer);
void vulkan_text_renderer_load_font(VulkanTextRenderer* renderer, FT_Face face);
void vulkan_text_render_text(VulkanTextRenderer* renderer, const char* text,
                             float x, float y, float scale, uint32_t color,
                             size_t window_id);
float vulkan_text_measure_text(VulkanTextRenderer* renderer, const char* text, float scale);
void vulkan_text_renderer_cleanup(VulkanTextRenderer* renderer);

#endif /* AROMA_VULKAN_TEXT_H */
#endif /* !ESP32 */
