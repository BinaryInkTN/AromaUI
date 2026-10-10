



















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

#endif
#endif
