#ifndef ESP32

#include "aroma_vulkan_text.h"
#include "helpers_vulkan.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include <string.h>
#include <stdlib.h>

static uint32_t vk_utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;
    if (c == 0)
        return 0;

    int len = 0;
    if (c < 0x80)
        len = 1;
    else if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    else
    {
        *p += 1;
        return 0xFFFD;
    }

    if (len == 1)
    {
        *p += 1;
        return c;
    }

    for (int i = 1; i < len; i++)
    {
        if (s[i] == 0 || (s[i] & 0xC0) != 0x80)
        {
            *p += 1;
            return 0xFFFD;
        }
    }

    uint32_t v = 0;
    if (len == 2)
        v = c & 0x1F;
    else if (len == 3)
        v = c & 0x0F;
    else if (len == 4)
        v = c & 0x07;

    for (int i = 1; i < len; i++)
        v = (v << 6) | (s[i] & 0x3F);

    *p += len;
    return v;
}

static bool create_glyph_texture(VulkanGlyph *glyph, const unsigned char *bitmap,
                                 int width, int height)
{
    AromaVulkanContext *ctx = vk_get_context();
    if (!ctx || !ctx->initialized || width == 0 || height == 0 || !bitmap)
        return false;

    VkDeviceSize imageSize = (VkDeviceSize)width * height;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    if (!vk_create_buffer(ctx, imageSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &stagingBuffer, &stagingMemory))
    {
        return false;
    }
    vk_copy_to_buffer(ctx, stagingMemory, bitmap, imageSize);

    if (!vk_create_image(ctx, (uint32_t)width, (uint32_t)height,
                         VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         &glyph->image, &glyph->memory))
    {
        vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
        vkFreeMemory(ctx->device, stagingMemory, NULL);
        return false;
    }

    vk_transition_image_layout(ctx, glyph->image,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_copy_buffer_to_image(ctx, stagingBuffer, glyph->image,
                            (uint32_t)width, (uint32_t)height);
    vk_transition_image_layout(ctx, glyph->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(ctx->device, stagingBuffer, NULL);
    vkFreeMemory(ctx->device, stagingMemory, NULL);

    glyph->imageView = vk_create_image_view(ctx, glyph->image,
                                            VK_FORMAT_R8_UNORM,
                                            VK_IMAGE_ASPECT_COLOR_BIT);
    if (glyph->imageView == VK_NULL_HANDLE)
    {
        vkDestroyImage(ctx->device, glyph->image, NULL);
        vkFreeMemory(ctx->device, glyph->memory, NULL);
        return false;
    }

    VkSamplerCreateInfo samplerInfo = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    };
    if (vkCreateSampler(ctx->device, &samplerInfo, NULL, &glyph->sampler) != VK_SUCCESS)
    {
        vkDestroyImageView(ctx->device, glyph->imageView, NULL);
        vkDestroyImage(ctx->device, glyph->image, NULL);
        vkFreeMemory(ctx->device, glyph->memory, NULL);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = ctx->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &ctx->textureDescriptorLayout,
    };
    if (vkAllocateDescriptorSets(ctx->device, &allocInfo, &glyph->descriptorSet) != VK_SUCCESS)
    {
        vkDestroySampler(ctx->device, glyph->sampler, NULL);
        vkDestroyImageView(ctx->device, glyph->imageView, NULL);
        vkDestroyImage(ctx->device, glyph->image, NULL);
        vkFreeMemory(ctx->device, glyph->memory, NULL);
        return false;
    }

    VkDescriptorImageInfo imageInfo = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = glyph->imageView,
        .sampler     = glyph->sampler,
    };
    VkWriteDescriptorSet descriptorWrite = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet           = glyph->descriptorSet,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount  = 1,
        .pImageInfo       = &imageInfo,
    };
    vkUpdateDescriptorSets(ctx->device, 1, &descriptorWrite, 0, NULL);

    glyph->valid = true;
    return true;
}

static void destroy_glyph_texture(VulkanGlyph *glyph)
{
    AromaVulkanContext *ctx = vk_get_context();
    if (!ctx || !ctx->initialized)
        return;

    if (glyph->descriptorSet != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(ctx->device, ctx->descriptorPool, 1, &glyph->descriptorSet);
        glyph->descriptorSet = VK_NULL_HANDLE;
    }
    if (glyph->sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx->device, glyph->sampler, NULL);
        glyph->sampler = VK_NULL_HANDLE;
    }
    if (glyph->imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(ctx->device, glyph->imageView, NULL);
        glyph->imageView = VK_NULL_HANDLE;
    }
    if (glyph->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(ctx->device, glyph->image, NULL);
        glyph->image = VK_NULL_HANDLE;
    }
    if (glyph->memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx->device, glyph->memory, NULL);
        glyph->memory = VK_NULL_HANDLE;
    }

    glyph->valid = false;
}

static void __init_glyph_from_slot(VulkanGlyph *glyph, uint32_t codepoint, FT_GlyphSlot g)
{
    memset(glyph, 0, sizeof(VulkanGlyph));
    glyph->codepoint = codepoint;
    glyph->width     = (int)g->bitmap.width;
    glyph->height    = (int)g->bitmap.rows;
    glyph->bearingX  = g->bitmap_left;
    glyph->bearingY  = g->bitmap_top;
    glyph->advance   = (int)(g->advance.x >> 6);
    glyph->valid     = false;

    if (g->bitmap.width > 0 && g->bitmap.rows > 0 && g->bitmap.buffer)
        create_glyph_texture(glyph, g->bitmap.buffer,
                             (int)g->bitmap.width, (int)g->bitmap.rows);
}

static VulkanGlyph *vk_get_glyph(VulkanTextRenderer *renderer, uint32_t codepoint)
{
    for (int i = 0; i < renderer->glyphCount; i++)
    {
        if (renderer->glyphs[i].codepoint == codepoint)
            return &renderer->glyphs[i];
    }

    if (renderer->glyphCount >= VK_TEXT_MAX_GLYPHS || !renderer->face)
        return NULL;

    FT_Error error = FT_Load_Char(renderer->face, codepoint, FT_LOAD_RENDER);
    if (error)
        return NULL;

    FT_GlyphSlot g = renderer->face->glyph;
    if (!g)
        return NULL;

    VulkanGlyph *glyph = &renderer->glyphs[renderer->glyphCount];
    __init_glyph_from_slot(glyph, codepoint, g);
    renderer->glyphCount++;
    return glyph;
}

int vulkan_text_renderer_init(VulkanTextRenderer *renderer)
{
    if (!renderer)
        return 0;
    memset(renderer, 0, sizeof(VulkanTextRenderer));
    return 1;
}

void vulkan_text_renderer_load_font(VulkanTextRenderer *renderer, FT_Face face)
{
    if (!renderer || !face)
        return;

    renderer->face       = face;
    renderer->fontHeight = (int)(face->size->metrics.height >> 6);
    renderer->glyphCount = 0;

    for (uint32_t c = 32; c < 127; c++)
    {
        FT_Error error = FT_Load_Char(face, c, FT_LOAD_RENDER);
        if (error)
            continue;

        FT_GlyphSlot g = face->glyph;
        if (!g)
            continue;
        if (renderer->glyphCount >= VK_TEXT_MAX_GLYPHS)
            break;

        VulkanGlyph *glyph = &renderer->glyphs[renderer->glyphCount];
        __init_glyph_from_slot(glyph, c, g);
        renderer->glyphCount++;
    }

    LOG_INFO("Vulkan text: Loaded %d initial glyphs", renderer->glyphCount);
}

void vulkan_text_render_text(VulkanTextRenderer *renderer, const char *text,
                             float x, float y, float scale, uint32_t color,
                             size_t window_id)
{
    if (!renderer || !text || scale <= 0.0f)
        return;

    AromaVulkanContext *ctx = vk_get_context();
    if (!ctx || !ctx->initialized || !ctx->inRenderPass)
        return;

    VkFrameData *frame = &ctx->frames[ctx->currentFrame];
    VkCommandBuffer cmd = frame->commandBuffer;

    float rgba[4];
    vk_convert_hex_to_rgba(rgba, color);

    VkVertex     textVerts[VK_MAX_TEXT_GLYPHS_PER_FRAME * 6];
    VulkanGlyph *textGlyphs[VK_MAX_TEXT_GLYPHS_PER_FRAME];
    int drawCount = 0;

    float currentX = x;
    const char *p = text;
    while (*p != '\0')
    {
        uint32_t codepoint = vk_utf8_next(&p);
        if (codepoint == 0)
            break;

        VulkanGlyph *g = vk_get_glyph(renderer, codepoint);
        if (!g)
            continue;

        if (!g->valid || g->width == 0 || g->height == 0)
        {
            currentX += (float)g->advance * scale;
            continue;
        }

        if (frame->textGlyphOffset + (uint32_t)drawCount >= VK_MAX_TEXT_GLYPHS_PER_FRAME)
            break;

        float xpos = currentX + (float)g->bearingX * scale;
        float ypos = y + (float)(renderer->fontHeight - g->bearingY) * scale;
        float w    = (float)g->width  * scale;
        float h    = (float)g->height * scale;

        textGlyphs[drawCount] = g;
        int vo = drawCount * 6;
        textVerts[vo + 0] = (VkVertex){.pos = {xpos,     ypos    }, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {0.0f, 0.0f}};
        textVerts[vo + 1] = (VkVertex){.pos = {xpos,     ypos + h}, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {0.0f, 1.0f}};
        textVerts[vo + 2] = (VkVertex){.pos = {xpos + w, ypos + h}, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {1.0f, 1.0f}};
        textVerts[vo + 3] = (VkVertex){.pos = {xpos,     ypos    }, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {0.0f, 0.0f}};
        textVerts[vo + 4] = (VkVertex){.pos = {xpos + w, ypos + h}, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {1.0f, 1.0f}};
        textVerts[vo + 5] = (VkVertex){.pos = {xpos + w, ypos    }, .col = {rgba[0], rgba[1], rgba[2], rgba[3]}, .texCoord = {1.0f, 0.0f}};
        drawCount++;

        currentX += (float)g->advance * scale;
    }

    if (drawCount == 0)
        return;

    VkDeviceSize baseOffset = (VkDeviceSize)frame->textGlyphOffset * 6 * sizeof(VkVertex);
    VkDeviceSize totalSize  = (VkDeviceSize)drawCount * 6 * sizeof(VkVertex);
    memcpy((char *)frame->textVertexMapped + baseOffset, textVerts, (size_t)totalSize);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->textPipeline);

    struct {
        mat4x4 projection;
        float  textColor[4];
    } textPC;
    memcpy(textPC.projection, ctx->pushConstants.projection, sizeof(mat4x4));
    textPC.textColor[0] = rgba[0];
    textPC.textColor[1] = rgba[1];
    textPC.textColor[2] = rgba[2];
    textPC.textColor[3] = rgba[3];
    vkCmdPushConstants(cmd, ctx->textPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(textPC), &textPC);

    VkDeviceSize vbOffset = baseOffset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->textVertexBuffer, &vbOffset);

    for (int i = 0; i < drawCount; i++)
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ctx->textPipelineLayout, 0, 1,
                                &textGlyphs[i]->descriptorSet, 0, NULL);
        vkCmdDraw(cmd, 6, 1, (uint32_t)i * 6, 0);
    }

    frame->textGlyphOffset += (uint32_t)drawCount;
}

float vulkan_text_measure_text(VulkanTextRenderer *renderer, const char *text, float scale)
{
    if (!renderer || !text || scale <= 0.0f)
        return 0.0f;

    float width = 0.0f;
    const char *p = text;
    while (*p != '\0')
    {
        uint32_t codepoint = vk_utf8_next(&p);
        if (codepoint == 0)
            break;

        VulkanGlyph *g = vk_get_glyph(renderer, codepoint);
        if (g)
            width += (float)g->advance * scale;
    }
    return width;
}

void vulkan_text_renderer_cleanup(VulkanTextRenderer *renderer)
{
    if (!renderer)
        return;

    for (int i = 0; i < renderer->glyphCount; i++)
        destroy_glyph_texture(&renderer->glyphs[i]);

    memset(renderer, 0, sizeof(VulkanTextRenderer));
}

#endif