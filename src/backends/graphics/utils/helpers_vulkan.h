
#ifndef HELPERS_VULKAN_H
#define HELPERS_VULKAN_H

#ifndef ESP32

#include <vulkan/vulkan.h>
#include "linmath.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../../platforms/aroma_platform_interface.h"

typedef struct VkVertex
{
    vec2 pos;
    vec4 col;
    vec2 texCoord;
} VkVertex;

#define VK_MAX_FRAMES_IN_FLIGHT 2
#define VK_MAX_BATCH_QUADS 256
#define VK_VERTS_PER_QUAD 6
#define VK_MAX_BATCH_VERTICES (VK_MAX_BATCH_QUADS * VK_VERTS_PER_QUAD)
#define VK_MAX_INDIVIDUAL_QUADS 512
#define VK_MAX_SHAPE_VERTICES (VK_MAX_BATCH_VERTICES + VK_MAX_INDIVIDUAL_QUADS * VK_VERTS_PER_QUAD)
#define VK_MAX_WINDOWS 256
#define VK_MAX_FONT_CACHE 16
#define VK_INVALID_FONT_INDEX (-1)
#define VK_MAX_TEXTURES 256
#define VK_MAX_TEXT_GLYPHS_PER_FRAME 1024

typedef struct VkTextureHandle
{
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    VkSampler sampler;
    VkDescriptorSet descriptorSet;
    int width;
    int height;
    uint32_t id;
    bool in_use;
} VkTextureHandle;

typedef struct VkFrameData
{
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailable;
    VkSemaphore renderFinished;
    VkFence inFlight;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    void *vertexMapped;
    VkBuffer textVertexBuffer;
    VkDeviceMemory textVertexMemory;
    void *textVertexMapped;
    uint32_t textGlyphOffset;
    uint32_t shapeVertexOffset;
} VkFrameData;

typedef struct AromaVulkanContext
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsFamily;
    VkQueue presentQueue;
    uint32_t presentFamily;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    uint32_t imageCount;
    VkImage *swapchainImages;
    VkImageView *swapchainImageViews;
    VkFramebuffer *framebuffers;

    VkRenderPass renderPass;
    VkRenderPass renderPassLoad;

    VkPipelineLayout shapePipelineLayout;
    VkPipeline shapePipeline;
    VkPipelineLayout textPipelineLayout;
    VkPipeline textPipeline;
    VkPipelineLayout imagePipelineLayout;
    VkPipeline imagePipeline;

    VkDescriptorSetLayout textureDescriptorLayout;
    VkDescriptorPool descriptorPool;

    VkCommandPool commandPool;

    VkFrameData frames[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame;

    VkTextureHandle textures[VK_MAX_TEXTURES];
    uint32_t nextTextureId;

    VkTextureHandle whiteTexture;

    struct
    {
        mat4x4 projection;
        float size[2];
        float radius;
        float borderWidth;
        int isRounded;
        int isHollow;
        int shapeType;
        int useTexture;
    } pushConstants;

    VkRect2D currentScissor;
    bool scissorEnabled;

    VkRect2D deferredScissor;
    bool hasDeferredScissor;

    float clearColor[4];

    bool srgbSwapchain;

#define VK_MAX_SWAPCHAIN_IMAGES 8
    struct
    {
        int x, y, w, h;
        bool hasDirty;
        bool everDrawn;
    } imageDirty[VK_MAX_SWAPCHAIN_IMAGES];
    bool partialUpdateActive;

    bool initialized;
    bool inRenderPass;
    uint32_t currentImageIndex;
    int width;
    int height;
} AromaVulkanContext;

bool vk_check_result(VkResult result, const char *operation);
uint32_t vk_find_memory_type(AromaVulkanContext *ctx, uint32_t typeFilter,
                             VkMemoryPropertyFlags properties);
VkShaderModule vk_create_shader_module(VkDevice device, const uint32_t *code, size_t size);

bool vk_create_buffer(AromaVulkanContext *ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *memory);
void vk_copy_to_buffer(AromaVulkanContext *ctx, VkDeviceMemory memory,
                       const void *data, VkDeviceSize size);

bool vk_create_image(AromaVulkanContext *ctx, uint32_t width, uint32_t height,
                     VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *memory);
VkImageView vk_create_image_view(AromaVulkanContext *ctx, VkImage image, VkFormat format,
                                 VkImageAspectFlags aspectFlags);
void vk_transition_image_layout(AromaVulkanContext *ctx, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout);
void vk_copy_buffer_to_image(AromaVulkanContext *ctx, VkBuffer buffer,
                             VkImage image, uint32_t width, uint32_t height);

VkCommandBuffer vk_begin_single_time_commands(AromaVulkanContext *ctx);
void vk_end_single_time_commands(AromaVulkanContext *ctx, VkCommandBuffer commandBuffer);

void vk_copy_to_buffer_offset(AromaVulkanContext *ctx, VkDeviceMemory memory,
                              VkDeviceSize offset, const void *data, VkDeviceSize size);

void vk_convert_hex_to_rgba(float *rgba, uint32_t color_hex);

AromaVulkanContext *vk_get_context(void);

#endif
#endif
