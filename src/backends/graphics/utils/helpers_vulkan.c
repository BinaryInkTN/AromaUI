
#ifndef ESP32

#include "helpers_vulkan.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool vk_check_result(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: %s failed with error %d", operation, (int)result);
        return false;
    }
    return true;
}

uint32_t vk_find_memory_type(AromaVulkanContext *ctx, uint32_t typeFilter,
                             VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    LOG_ERROR("Vulkan: Failed to find suitable memory type");
    return UINT32_MAX;
}

VkShaderModule vk_create_shader_module(VkDevice device, const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

bool vk_create_buffer(AromaVulkanContext *ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, buffer) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to create buffer");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device, *buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vk_find_memory_type(ctx, memRequirements.memoryTypeBits, properties),
    };

    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(ctx->device, *buffer, NULL);
        return false;
    }

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, memory) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to allocate buffer memory");
        vkDestroyBuffer(ctx->device, *buffer, NULL);
        return false;
    }

    vkBindBufferMemory(ctx->device, *buffer, *memory, 0);
    return true;
}

void vk_copy_to_buffer(AromaVulkanContext *ctx, VkDeviceMemory memory,
                       const void *data, VkDeviceSize size)
{
    void *mapped;
    vkMapMemory(ctx->device, memory, 0, size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(ctx->device, memory);
}

void vk_copy_to_buffer_offset(AromaVulkanContext *ctx, VkDeviceMemory memory,
                              VkDeviceSize offset, const void *data, VkDeviceSize size)
{
    void *mapped;
    vkMapMemory(ctx->device, memory, 0, offset + size, 0, &mapped);
    memcpy((char *)mapped + offset, data, (size_t)size);
    vkUnmapMemory(ctx->device, memory);
}

bool vk_create_image(AromaVulkanContext *ctx, uint32_t width, uint32_t height,
                     VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *memory)
{
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateImage(ctx->device, &imageInfo, NULL, image) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to create image");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(ctx->device, *image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vk_find_memory_type(ctx, memRequirements.memoryTypeBits, properties),
    };

    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyImage(ctx->device, *image, NULL);
        return false;
    }

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, memory) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to allocate image memory");
        vkDestroyImage(ctx->device, *image, NULL);
        return false;
    }

    vkBindImageMemory(ctx->device, *image, *memory, 0);
    return true;
}

VkImageView vk_create_image_view(AromaVulkanContext *ctx, VkImage image, VkFormat format,
                                 VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange.aspectMask = aspectFlags,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };

    VkImageView imageView;
    if (vkCreateImageView(ctx->device, &viewInfo, NULL, &imageView) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to create image view");
        return VK_NULL_HANDLE;
    }
    return imageView;
}

VkCommandBuffer vk_begin_single_time_commands(AromaVulkanContext *ctx)
{
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = ctx->commandPool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void vk_end_single_time_commands(AromaVulkanContext *ctx, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };

    vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphicsQueue);
    vkFreeCommandBuffers(ctx->device, ctx->commandPool, 1, &commandBuffer);
}

void vk_transition_image_layout(AromaVulkanContext *ctx, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer = vk_begin_single_time_commands(ctx);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };

    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0,
                         0, NULL, 0, NULL, 1, &barrier);

    vk_end_single_time_commands(ctx, commandBuffer);
}

void vk_copy_buffer_to_image(AromaVulkanContext *ctx, VkBuffer buffer,
                             VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer commandBuffer = vk_begin_single_time_commands(ctx);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vk_end_single_time_commands(ctx, commandBuffer);
}

static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

void vk_convert_hex_to_rgba(float *rgba, uint32_t color_hex)
{
    rgba[0] = ((color_hex >> 16) & 0xFF) / 255.0f;
    rgba[1] = ((color_hex >> 8) & 0xFF) / 255.0f;
    rgba[2] = ((color_hex) & 0xFF) / 255.0f;
    uint8_t alpha_byte = (color_hex >> 24) & 0xFF;
    rgba[3] = (alpha_byte == 0) ? 1.0f : alpha_byte / 255.0f;

    AromaVulkanContext *ctx = vk_get_context();
    if (ctx && ctx->srgbSwapchain)
    {
        rgba[0] = srgb_to_linear(rgba[0]);
        rgba[1] = srgb_to_linear(rgba[1]);
        rgba[2] = srgb_to_linear(rgba[2]);
    }
}

#endif
