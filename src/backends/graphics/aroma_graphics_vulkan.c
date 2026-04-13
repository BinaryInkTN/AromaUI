
#ifndef ESP32
#include "aroma_graphics_interface.h"
#include "utils/helpers_vulkan.h"
#include "utils/aroma_vulkan_text.h"
#include "aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_ui.h"
#include "core/aroma_logger.h"
#include "core/aroma_font.h"
#include "core/aroma_node.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "utils/stb_image.h"

#include "utils/aroma_vulkan_shaders.h"

typedef struct
{
    VulkanTextRenderer renderer;
    AromaFont *font;
    uint32_t lastUsedFrame;
    uint32_t fontId;
} VkCachedFontRenderer;

typedef struct
{
    VkCachedFontRenderer fontCache[VK_MAX_FONT_CACHE];
    int fontCacheCount;
} VkWindowResources;

typedef struct
{
    VkVertex vertices[VK_MAX_BATCH_VERTICES];
    int count;
    size_t windowId;
} VkShapeBatch;

static AromaVulkanContext vk_ctx = {0};
static VkWindowResources vk_windows[VK_MAX_WINDOWS] = {0};
static VkShapeBatch vk_batch = {0};
static uint32_t vk_current_frame_number = 0;

AromaVulkanContext *vk_get_context(void) { return &vk_ctx; }

static bool create_instance(void);
static bool pick_physical_device(void);
static bool create_logical_device(void);
static bool create_swapchain(int width, int height);
static bool create_render_pass(void);
static bool create_descriptor_set_layout(void);
static bool create_graphics_pipelines(void);
static bool create_framebuffers(void);
static bool create_command_pool(void);
static bool create_command_buffers(void);
static bool create_sync_objects(void);
static bool create_vertex_buffers(void);
static bool create_descriptor_pool(void);
static bool create_white_texture(void);
static void cleanup_swapchain(void);
static bool recreate_swapchain(void);
static bool begin_frame(size_t window_id);
static void end_render_pass(void);
static void flush_shape_batch(void);
static bool ensure_frame_state(size_t window_id);

static int find_font_in_cache(size_t window_id, AromaFont *font)
{
    if (window_id >= VK_MAX_WINDOWS)
        return VK_INVALID_FONT_INDEX;
    VkWindowResources *win = &vk_windows[window_id];
    for (int i = 0; i < win->fontCacheCount; i++)
    {
        if (win->fontCache[i].font == font)
        {
            win->fontCache[i].lastUsedFrame = vk_current_frame_number;
            return i;
        }
    }
    return VK_INVALID_FONT_INDEX;
}

static int evict_lru_font(size_t window_id)
{
    if (window_id >= VK_MAX_WINDOWS)
        return VK_INVALID_FONT_INDEX;
    VkWindowResources *win = &vk_windows[window_id];
    if (win->fontCacheCount == 0)
        return VK_INVALID_FONT_INDEX;

    int lru = 0;
    uint32_t oldest = win->fontCache[0].lastUsedFrame;
    for (int i = 1; i < win->fontCacheCount; i++)
    {
        if (win->fontCache[i].lastUsedFrame < oldest)
        {
            oldest = win->fontCache[i].lastUsedFrame;
            lru = i;
        }
    }
    vulkan_text_renderer_cleanup(&win->fontCache[lru].renderer);
    for (int i = lru; i < win->fontCacheCount - 1; i++)
        win->fontCache[i] = win->fontCache[i + 1];
    win->fontCacheCount--;
    return lru;
}

static VulkanTextRenderer *get_or_load_font(size_t window_id, AromaFont *font)
{
    if (window_id >= VK_MAX_WINDOWS || !font)
        return NULL;
    int idx = find_font_in_cache(window_id, font);
    if (idx != VK_INVALID_FONT_INDEX)
        return &vk_windows[window_id].fontCache[idx].renderer;

    VkWindowResources *win = &vk_windows[window_id];
    if (win->fontCacheCount >= VK_MAX_FONT_CACHE)
        evict_lru_font(window_id);

    int ni = win->fontCacheCount;
    VkCachedFontRenderer *cache = &win->fontCache[ni];
    if (!vulkan_text_renderer_init(&cache->renderer))
    {
        LOG_ERROR("Vulkan: Failed to init text renderer for window %zu", window_id);
        return NULL;
    }

    FT_Face face = (FT_Face)aroma_font_get_face(font);
    if (!face)
    {
        LOG_ERROR("Vulkan: Failed to get font face for window %zu", window_id);
        return NULL;
    }

    vulkan_text_renderer_load_font(&cache->renderer, face);
    cache->font = font;
    cache->lastUsedFrame = vk_current_frame_number;
    cache->fontId = (uint32_t)(uintptr_t)font;
    win->fontCacheCount++;

    LOG_INFO("Vulkan: Loaded new font for window %zu (total: %d)", window_id, win->fontCacheCount);
    return &cache->renderer;
}

static bool create_instance(void)
{
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "AromaUI",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "AromaUI Vulkan",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    uint32_t extCount = 0;
    const char **extensions = NULL;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_vulkan_instance_extensions)
    {
        extensions = platform->get_vulkan_instance_extensions(&extCount);
    }

    if (!extensions || extCount == 0)
    {
        static const char *fallback[] = {"VK_KHR_surface"};
        extensions = fallback;
        extCount = 1;
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = extCount,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = 0,
    };

#ifndef NDEBUG
    const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties *available = malloc(layerCount * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layerCount, available);
    bool hasValidation = false;
    for (uint32_t i = 0; i < layerCount; i++)
    {
        if (strcmp(available[i].layerName, validationLayers[0]) == 0)
        {
            hasValidation = true;
            break;
        }
    }
    free(available);
    if (hasValidation)
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }
#endif

    return vk_check_result(
        vkCreateInstance(&createInfo, NULL, &vk_ctx.instance),
        "vkCreateInstance");
}

static bool pick_physical_device(void)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vk_ctx.instance, &deviceCount, NULL);
    if (deviceCount == 0)
    {
        LOG_ERROR("Vulkan: No GPUs with Vulkan support");
        return false;
    }

    VkPhysicalDevice *devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vk_ctx.instance, &deviceCount, devices);

    for (uint32_t d = 0; d < deviceCount; d++)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queueFamilyCount, NULL);
        VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queueFamilyCount, queueFamilies);

        bool foundGraphics = false;
        for (uint32_t q = 0; q < queueFamilyCount; q++)
        {
            if (queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                vk_ctx.physicalDevice = devices[d];
                vk_ctx.graphicsFamily = q;
                vk_ctx.presentFamily = q;
                foundGraphics = true;
                break;
            }
        }
        free(queueFamilies);
        if (foundGraphics)
            break;
    }
    free(devices);

    if (vk_ctx.physicalDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan: Failed to find suitable GPU");
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_ctx.physicalDevice, &props);
    LOG_INFO("Vulkan: Selected GPU: %s", props.deviceName);
    return true;
}

static bool create_logical_device(void)
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk_ctx.graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures = {0};

    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .pEnabledFeatures = &deviceFeatures,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = deviceExtensions,
    };

    if (!vk_check_result(
            vkCreateDevice(vk_ctx.physicalDevice, &createInfo, NULL, &vk_ctx.device),
            "vkCreateDevice"))
    {
        return false;
    }

    vkGetDeviceQueue(vk_ctx.device, vk_ctx.graphicsFamily, 0, &vk_ctx.graphicsQueue);
    vkGetDeviceQueue(vk_ctx.device, vk_ctx.presentFamily, 0, &vk_ctx.presentQueue);
    return true;
}

static bool create_surface(size_t window_id)
{
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (!platform)
        return false;

    if (platform->create_vulkan_surface)
    {
        bool ok = platform->create_vulkan_surface(window_id, &vk_ctx.instance, &vk_ctx.surface);
        if (ok)
        {
            LOG_INFO("Vulkan: Surface created via platform backend");
            return true;
        }
    }

    LOG_ERROR("Vulkan: Platform does not support Vulkan surface creation");
    return false;
}

static bool create_swapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_ctx.physicalDevice, vk_ctx.surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_ctx.physicalDevice, vk_ctx.surface, &formatCount, NULL);
    VkSurfaceFormatKHR *formats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_ctx.physicalDevice, vk_ctx.surface, &formatCount, formats);

    VkSurfaceFormatKHR chosenFormat = formats[0];

    for (uint32_t i = 0; i < formatCount; i++)
    {
        if ((formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
             formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosenFormat = formats[i];
            break;
        }
    }

    if (chosenFormat.format == formats[0].format && chosenFormat.colorSpace == formats[0].colorSpace)
    {
        for (uint32_t i = 0; i < formatCount; i++)
        {
            if ((formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
                 formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosenFormat = formats[i];
                break;
            }
        }
    }
    free(formats);

    vk_ctx.srgbSwapchain = (chosenFormat.format == VK_FORMAT_R8G8B8A8_UNORM ||
                            chosenFormat.format == VK_FORMAT_B8G8R8A8_SRGB);
    LOG_INFO("Vulkan swapchain format: %d  colorSpace: %d  srgb: %s",
             (int)chosenFormat.format, (int)chosenFormat.colorSpace,
             vk_ctx.srgbSwapchain ? "yes" : "no");

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        extent = capabilities.currentExtent;
    }
    else
    {
        extent.width = (uint32_t)width;
        extent.height = (uint32_t)height;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (uint32_t i = 0; i < 4; i++)
    {
        if (capabilities.supportedCompositeAlpha & compositeAlphaFlags[i])
        {
            compositeAlpha = compositeAlphaFlags[i];
            break;
        }
    }

       VkSurfaceTransformFlagBitsKHR preTransform;
    if (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    else
        preTransform = capabilities.currentTransform;

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk_ctx.surface,
        .minImageCount = imageCount,
        .imageFormat = chosenFormat.format,
        .imageColorSpace = chosenFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = preTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    uint32_t queueFamilyIndices[] = {vk_ctx.graphicsFamily, vk_ctx.presentFamily};
    if (vk_ctx.graphicsFamily != vk_ctx.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (!vk_check_result(
            vkCreateSwapchainKHR(vk_ctx.device, &createInfo, NULL, &vk_ctx.swapchain),
            "vkCreateSwapchainKHR"))
    {
        return false;
    }

    vk_ctx.swapchainFormat = chosenFormat.format;
    vk_ctx.swapchainExtent = extent;

    vkGetSwapchainImagesKHR(vk_ctx.device, vk_ctx.swapchain, &vk_ctx.imageCount, NULL);
    vk_ctx.swapchainImages = malloc(vk_ctx.imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(vk_ctx.device, vk_ctx.swapchain, &vk_ctx.imageCount, vk_ctx.swapchainImages);

    vk_ctx.swapchainImageViews = malloc(vk_ctx.imageCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
    {
        vk_ctx.swapchainImageViews[i] = vk_create_image_view(
            &vk_ctx, vk_ctx.swapchainImages[i], vk_ctx.swapchainFormat,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    vk_ctx.width = (int)extent.width;
    vk_ctx.height = (int)extent.height;

    LOG_INFO("Vulkan: Swapchain created %ux%u, %u images", extent.width, extent.height, vk_ctx.imageCount);
    return true;
}

static bool create_render_pass(void)
{
    VkAttachmentDescription colorAttachment = {
        .format = vk_ctx.swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
    };

    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dep,
    };

    if (!vk_check_result(vkCreateRenderPass(vk_ctx.device, &renderPassInfo, NULL, &vk_ctx.renderPass),
                         "vkCreateRenderPass (clear)"))
    {
        return false;
    }

    VkAttachmentDescription loadAttachment = colorAttachment;
    loadAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    loadAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkRenderPassCreateInfo loadRPInfo = renderPassInfo;
    loadRPInfo.pAttachments = &loadAttachment;

    VkSubpassDependency loadDep = dep;
    loadDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    loadDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    loadRPInfo.pDependencies = &loadDep;

    return vk_check_result(
        vkCreateRenderPass(vk_ctx.device, &loadRPInfo, NULL, &vk_ctx.renderPassLoad),
        "vkCreateRenderPass (load)");
}

static bool create_descriptor_set_layout(void)
{
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };

    return vk_check_result(
        vkCreateDescriptorSetLayout(vk_ctx.device, &layoutInfo, NULL, &vk_ctx.textureDescriptorLayout),
        "vkCreateDescriptorSetLayout");
}

static bool create_descriptor_pool(void)
{
    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 8192,
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
        .maxSets = 8192,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };

    return vk_check_result(
        vkCreateDescriptorPool(vk_ctx.device, &poolInfo, NULL, &vk_ctx.descriptorPool),
        "vkCreateDescriptorPool");
}

static VkVertexInputBindingDescription get_binding_description(void)
{
    VkVertexInputBindingDescription desc = {
        .binding = 0,
        .stride = sizeof(VkVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    return desc;
}

static void get_attribute_descriptions(VkVertexInputAttributeDescription *attrs)
{

    attrs[0] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(VkVertex, pos),
    };

    attrs[1] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 1,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = offsetof(VkVertex, col),
    };

    attrs[2] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 2,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(VkVertex, texCoord),
    };
}

static bool create_pipeline(VkShaderModule vertModule, VkShaderModule fragModule,
                            VkPipelineLayout *outLayout, VkPipeline *outPipeline,
                            bool hasDescriptorSet, bool enableBlend)
{
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName = "main",
        },
    };

    VkVertexInputBindingDescription bindingDesc = get_binding_description();
    VkVertexInputAttributeDescription attrDescs[3];
    get_attribute_descriptions(attrDescs);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDesc,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = attrDescs,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = enableBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = 128,
    };

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
        .setLayoutCount = hasDescriptorSet ? 1 : 0,
        .pSetLayouts = hasDescriptorSet ? &vk_ctx.textureDescriptorLayout : NULL,
    };

    if (!vk_check_result(
            vkCreatePipelineLayout(vk_ctx.device, &layoutInfo, NULL, outLayout),
            "vkCreatePipelineLayout"))
    {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = *outLayout,
        .renderPass = vk_ctx.renderPass,
        .subpass = 0,
    };

    return vk_check_result(
        vkCreateGraphicsPipelines(vk_ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, outPipeline),
        "vkCreateGraphicsPipelines");
}

static bool create_graphics_pipelines(void)
{

    VkShaderModule shapeVert = vk_create_shader_module(vk_ctx.device,
                                                       aroma_vk_shape_vert_spv, sizeof(aroma_vk_shape_vert_spv));
    VkShaderModule shapeFrag = vk_create_shader_module(vk_ctx.device,
                                                       aroma_vk_shape_frag_spv, sizeof(aroma_vk_shape_frag_spv));

    if (shapeVert == VK_NULL_HANDLE || shapeFrag == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan: Failed to create shape shader modules");
        return false;
    }

    if (!create_pipeline(shapeVert, shapeFrag,
                         &vk_ctx.shapePipelineLayout, &vk_ctx.shapePipeline,
                         false, true))
    {
        return false;
    }
    vkDestroyShaderModule(vk_ctx.device, shapeVert, NULL);
    vkDestroyShaderModule(vk_ctx.device, shapeFrag, NULL);

    VkShaderModule textVert = vk_create_shader_module(vk_ctx.device,
                                                      aroma_vk_text_vert_spv, sizeof(aroma_vk_text_vert_spv));
    VkShaderModule textFrag = vk_create_shader_module(vk_ctx.device,
                                                      aroma_vk_text_frag_spv, sizeof(aroma_vk_text_frag_spv));

    if (textVert == VK_NULL_HANDLE || textFrag == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan: Failed to create text shader modules");
        return false;
    }

    if (!create_pipeline(textVert, textFrag,
                         &vk_ctx.textPipelineLayout, &vk_ctx.textPipeline,
                         true, true))
    {
        return false;
    }
    vkDestroyShaderModule(vk_ctx.device, textVert, NULL);
    vkDestroyShaderModule(vk_ctx.device, textFrag, NULL);

    VkShaderModule imgVert = vk_create_shader_module(vk_ctx.device,
                                                     aroma_vk_image_vert_spv, sizeof(aroma_vk_image_vert_spv));
    VkShaderModule imgFrag = vk_create_shader_module(vk_ctx.device,
                                                     aroma_vk_image_frag_spv, sizeof(aroma_vk_image_frag_spv));

    if (imgVert == VK_NULL_HANDLE || imgFrag == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan: Failed to create image shader modules");
        return false;
    }

    if (!create_pipeline(imgVert, imgFrag,
                         &vk_ctx.imagePipelineLayout, &vk_ctx.imagePipeline,
                         true, true))
    {
        return false;
    }
    vkDestroyShaderModule(vk_ctx.device, imgVert, NULL);
    vkDestroyShaderModule(vk_ctx.device, imgFrag, NULL);

    return true;
}

static bool create_framebuffers(void)
{
    vk_ctx.framebuffers = malloc(vk_ctx.imageCount * sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
    {
        VkFramebufferCreateInfo fbInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk_ctx.renderPass,
            .attachmentCount = 1,
            .pAttachments = &vk_ctx.swapchainImageViews[i],
            .width = vk_ctx.swapchainExtent.width,
            .height = vk_ctx.swapchainExtent.height,
            .layers = 1,
        };
        if (!vk_check_result(
                vkCreateFramebuffer(vk_ctx.device, &fbInfo, NULL, &vk_ctx.framebuffers[i]),
                "vkCreateFramebuffer"))
        {
            return false;
        }
    }
    return true;
}

static bool create_command_pool(void)
{
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk_ctx.graphicsFamily,
    };
    return vk_check_result(
        vkCreateCommandPool(vk_ctx.device, &poolInfo, NULL, &vk_ctx.commandPool),
        "vkCreateCommandPool");
}

static bool create_command_buffers(void)
{
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_ctx.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VK_MAX_FRAMES_IN_FLIGHT,
    };
    VkCommandBuffer bufs[VK_MAX_FRAMES_IN_FLIGHT];
    if (!vk_check_result(
            vkAllocateCommandBuffers(vk_ctx.device, &allocInfo, bufs),
            "vkAllocateCommandBuffers"))
    {
        return false;
    }
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
        vk_ctx.frames[i].commandBuffer = bufs[i];
    return true;
}

static bool create_sync_objects(void)
{
    VkSemaphoreCreateInfo semInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(vk_ctx.device, &semInfo, NULL, &vk_ctx.frames[i].imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(vk_ctx.device, &semInfo, NULL, &vk_ctx.frames[i].renderFinished) != VK_SUCCESS ||
            vkCreateFence(vk_ctx.device, &fenceInfo, NULL, &vk_ctx.frames[i].inFlight) != VK_SUCCESS)
        {
            LOG_ERROR("Vulkan: Failed to create sync objects");
            return false;
        }
    }
    return true;
}

static bool create_vertex_buffers(void)
{
    VkDeviceSize shapeSize = sizeof(VkVertex) * VK_MAX_SHAPE_VERTICES;
    VkDeviceSize textSize = sizeof(VkVertex) * 6 * VK_MAX_TEXT_GLYPHS_PER_FRAME;

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (!vk_create_buffer(&vk_ctx, shapeSize,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &vk_ctx.frames[i].vertexBuffer,
                              &vk_ctx.frames[i].vertexMemory))
        {
            return false;
        }

        if (vkMapMemory(vk_ctx.device, vk_ctx.frames[i].vertexMemory,
                        0, shapeSize, 0, &vk_ctx.frames[i].vertexMapped) != VK_SUCCESS)
        {
            LOG_ERROR("Vulkan: Failed to persistently map shape vertex buffer");
            return false;
        }
        if (!vk_create_buffer(&vk_ctx, textSize,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &vk_ctx.frames[i].textVertexBuffer,
                              &vk_ctx.frames[i].textVertexMemory))
        {
            return false;
        }
        if (vkMapMemory(vk_ctx.device, vk_ctx.frames[i].textVertexMemory,
                        0, textSize, 0, &vk_ctx.frames[i].textVertexMapped) != VK_SUCCESS)
        {
            LOG_ERROR("Vulkan: Failed to persistently map text vertex buffer");
            return false;
        }
    }
    return true;
}

static bool create_white_texture(void)
{
    uint32_t white = 0xFFFFFFFF;
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    if (!vk_create_buffer(&vk_ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &stagingBuffer, &stagingMemory))
        return false;
    vk_copy_to_buffer(&vk_ctx, stagingMemory, &white, imageSize);

    if (!vk_create_image(&vk_ctx, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         &vk_ctx.whiteTexture.image, &vk_ctx.whiteTexture.memory))
    {
        vkDestroyBuffer(vk_ctx.device, stagingBuffer, NULL);
        vkFreeMemory(vk_ctx.device, stagingMemory, NULL);
        return false;
    }

    vk_transition_image_layout(&vk_ctx, vk_ctx.whiteTexture.image,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_copy_buffer_to_image(&vk_ctx, stagingBuffer, vk_ctx.whiteTexture.image, 1, 1);
    vk_transition_image_layout(&vk_ctx, vk_ctx.whiteTexture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(vk_ctx.device, stagingBuffer, NULL);
    vkFreeMemory(vk_ctx.device, stagingMemory, NULL);

    vk_ctx.whiteTexture.imageView = vk_create_image_view(&vk_ctx, vk_ctx.whiteTexture.image,
                                                         VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(vk_ctx.device, &samplerInfo, NULL, &vk_ctx.whiteTexture.sampler);

    vk_ctx.whiteTexture.width = 1;
    vk_ctx.whiteTexture.height = 1;
    vk_ctx.whiteTexture.in_use = true;

    return true;
}

static void cleanup_swapchain(void)
{
    if (vk_ctx.framebuffers)
    {
        for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
            vkDestroyFramebuffer(vk_ctx.device, vk_ctx.framebuffers[i], NULL);
        free(vk_ctx.framebuffers);
        vk_ctx.framebuffers = NULL;
    }
    if (vk_ctx.swapchainImageViews)
    {
        for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
            vkDestroyImageView(vk_ctx.device, vk_ctx.swapchainImageViews[i], NULL);
        free(vk_ctx.swapchainImageViews);
        vk_ctx.swapchainImageViews = NULL;
    }
    free(vk_ctx.swapchainImages);
    vk_ctx.swapchainImages = NULL;
    if (vk_ctx.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(vk_ctx.device, vk_ctx.swapchain, NULL);
        vk_ctx.swapchain = VK_NULL_HANDLE;
    }
}

static bool recreate_swapchain(void)
{
    vkDeviceWaitIdle(vk_ctx.device);
    cleanup_swapchain();
    bool ok = create_swapchain(vk_ctx.width, vk_ctx.height) &&
              create_framebuffers();

    for (int i = 0; i < VK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        vk_ctx.imageDirty[i].hasDirty = false;
        vk_ctx.imageDirty[i].everDrawn = false;
    }
    return ok;
}

static bool ensure_frame_state(size_t window_id)
{
    if (vk_ctx.inRenderPass)
        return true;

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (!platform)
        return false;

    int w = 0, h = 0;
    if (platform->get_window_size)
        platform->get_window_size(window_id, &w, &h);
    if (w <= 0 || h <= 0)
        return false;

    if (w != vk_ctx.width || h != vk_ctx.height)
    {
        vk_ctx.width = w;
        vk_ctx.height = h;
        if (!recreate_swapchain())
            return false;
    }

    return begin_frame(window_id);
}

static bool begin_frame(size_t window_id)
{
    if (vk_ctx.inRenderPass)
        return true;

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];

    vkWaitForFences(vk_ctx.device, 1, &frame->inFlight, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        vk_ctx.device, vk_ctx.swapchain, UINT64_MAX,
        frame->imageAvailable, VK_NULL_HANDLE, &vk_ctx.currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        if (!recreate_swapchain())
            return false;
        return begin_frame(window_id);
    }
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan: Failed to acquire swapchain image");
        return false;
    }

    vkResetFences(vk_ctx.device, 1, &frame->inFlight);
    vkResetCommandBuffer(frame->commandBuffer, 0);
    frame->textGlyphOffset = 0;
    frame->shapeVertexOffset = 0;

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(frame->commandBuffer, &beginInfo);

    uint32_t imgIdx = vk_ctx.currentImageIndex;
    bool usePartial = vk_ctx.imageDirty[imgIdx].everDrawn &&
                      vk_ctx.imageDirty[imgIdx].hasDirty;

    if (usePartial)
    {
        int screenArea = (int)vk_ctx.swapchainExtent.width * (int)vk_ctx.swapchainExtent.height;
        int dirtyArea = vk_ctx.imageDirty[imgIdx].w * vk_ctx.imageDirty[imgIdx].h;
        if (dirtyArea > screenArea / 2)
            usePartial = false;
    }

    VkClearValue clearValue = {{{vk_ctx.clearColor[0], vk_ctx.clearColor[1],
                                 vk_ctx.clearColor[2], vk_ctx.clearColor[3]}}};
    VkRenderPassBeginInfo rpInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = usePartial ? vk_ctx.renderPassLoad : vk_ctx.renderPass,
        .framebuffer = vk_ctx.framebuffers[vk_ctx.currentImageIndex],
        .renderArea.offset = {0, 0},
        .renderArea.extent = vk_ctx.swapchainExtent,
        .clearValueCount = usePartial ? 0 : 1,
        .pClearValues = usePartial ? NULL : &clearValue,
    };
    vkCmdBeginRenderPass(frame->commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    float vp_height = (float)vk_ctx.swapchainExtent.height;
    VkViewport viewport = {
        .x = 0.0f,
        .y = vp_height,
        .width = (float)vk_ctx.swapchainExtent.width,
        .height = -vp_height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(frame->commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = vk_ctx.swapchainExtent,
    };

    vk_ctx.partialUpdateActive = false;

    if (usePartial)
    {

        int dx = vk_ctx.imageDirty[imgIdx].x;
        int dy = vk_ctx.imageDirty[imgIdx].y;
        int dw = vk_ctx.imageDirty[imgIdx].w;
        int dh = vk_ctx.imageDirty[imgIdx].h;

        if (dx < 0)
        {
            dw += dx;
            dx = 0;
        }
        if (dy < 0)
        {
            dh += dy;
            dy = 0;
        }
        if (dx + dw > (int)vk_ctx.swapchainExtent.width)
            dw = (int)vk_ctx.swapchainExtent.width - dx;
        if (dy + dh > (int)vk_ctx.swapchainExtent.height)
            dh = (int)vk_ctx.swapchainExtent.height - dy;

        if (dw > 0 && dh > 0)
        {
            scissor.offset.x = dx;
            scissor.offset.y = dy;
            scissor.extent.width = (uint32_t)dw;
            scissor.extent.height = (uint32_t)dh;
            vk_ctx.partialUpdateActive = true;

            VkClearAttachment clearAtt = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0,
                .clearValue.color.float32 = {
                    vk_ctx.clearColor[0], vk_ctx.clearColor[1],
                    vk_ctx.clearColor[2], vk_ctx.clearColor[3]},
            };
            VkClearRect clearRect = {
                .rect = scissor,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            vkCmdClearAttachments(frame->commandBuffer, 1, &clearAtt, 1, &clearRect);
        }
    }

    if (vk_ctx.hasDeferredScissor)
    {
        scissor = vk_ctx.deferredScissor;
        vk_ctx.scissorEnabled = true;
        vk_ctx.hasDeferredScissor = false;
    }

    vk_ctx.currentScissor = scissor;
    vkCmdSetScissor(frame->commandBuffer, 0, 1, &scissor);

    mat4x4_ortho(vk_ctx.pushConstants.projection,
                 0.0f, (float)vk_ctx.swapchainExtent.width,
                 (float)vk_ctx.swapchainExtent.height, 0.0f, -1.0f, 1.0f);

    vk_ctx.imageDirty[imgIdx].everDrawn = true;
    vk_ctx.imageDirty[imgIdx].hasDirty = false;

    vk_ctx.inRenderPass = true;
    return true;
}

static void end_render_pass(void)
{
    if (!vk_ctx.inRenderPass)
        return;

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];
    vkCmdEndRenderPass(frame->commandBuffer);
    vkEndCommandBuffer(frame->commandBuffer);

    VkSemaphore waitSemaphores[] = {frame->imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {frame->renderFinished};

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores,
    };
    vkQueueSubmit(vk_ctx.graphicsQueue, 1, &submitInfo, frame->inFlight);

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = &vk_ctx.swapchain,
        .pImageIndices = &vk_ctx.currentImageIndex,
    };
    VkResult result = vkQueuePresentKHR(vk_ctx.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreate_swapchain();

        extern AromaWindowHandle g_windows[];
        for (int i = 0; i < AROMA_MAX_WINDOWS; i++)
        {
            if (g_windows[i].root_node)
                aroma_node_invalidate(g_windows[i].root_node);
        }
    }

    vk_ctx.currentFrame = (vk_ctx.currentFrame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
    vk_ctx.inRenderPass = false;
}

static void flush_shape_batch(void)
{
    if (vk_batch.count == 0)
        return;
    if (!ensure_frame_state(vk_batch.windowId))
    {
        vk_batch.count = 0;
        return;
    }

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];
    VkCommandBuffer cmd = frame->commandBuffer;

    if (frame->shapeVertexOffset + (uint32_t)vk_batch.count > VK_MAX_SHAPE_VERTICES)
    {
        vk_batch.count = 0;
        return;
    }

    VkDeviceSize byteOffset = (VkDeviceSize)frame->shapeVertexOffset * sizeof(VkVertex);
    memcpy((char *)frame->vertexMapped + byteOffset,
           vk_batch.vertices,
           (size_t)(vk_batch.count * sizeof(VkVertex)));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_ctx.shapePipeline);

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
    } batchPC;
    memcpy(batchPC.projection, vk_ctx.pushConstants.projection, sizeof(mat4x4));
    batchPC.size[0] = 0.0f;
    batchPC.size[1] = 0.0f;
    batchPC.radius = 0.0f;
    batchPC.borderWidth = 0.0f;
    batchPC.isRounded = 0;
    batchPC.isHollow = 0;
    batchPC.shapeType = 0;
    batchPC.useTexture = 0;
    vkCmdPushConstants(cmd, vk_ctx.shapePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(batchPC), &batchPC);

    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertexBuffer, &byteOffset);
    vkCmdDraw(cmd, (uint32_t)vk_batch.count, 1, 0, 0);

    frame->shapeVertexOffset += (uint32_t)vk_batch.count;
    vk_batch.count = 0;
}

static void batch_add_rect(size_t window_id, int x, int y, int w, int h, uint32_t color)
{
    if (vk_batch.count > 0 && vk_batch.windowId != window_id)
        flush_shape_batch();
    if (vk_batch.count + VK_VERTS_PER_QUAD > VK_MAX_BATCH_VERTICES)
        flush_shape_batch();

    vk_batch.windowId = window_id;

    float rgba[4];
    vk_convert_hex_to_rgba(rgba, color);

    float x0 = (float)x, y0 = (float)y;
    float x1 = x0 + (float)w, y1 = y0 + (float)h;

    VkVertex *v = &vk_batch.vertices[vk_batch.count];
    v[0].pos[0] = x0;
    v[0].pos[1] = y0;
    v[1].pos[0] = x1;
    v[1].pos[1] = y0;
    v[2].pos[0] = x0;
    v[2].pos[1] = y1;
    v[3].pos[0] = x1;
    v[3].pos[1] = y0;
    v[4].pos[0] = x1;
    v[4].pos[1] = y1;
    v[5].pos[0] = x0;
    v[5].pos[1] = y1;

    for (int i = 0; i < VK_VERTS_PER_QUAD; i++)
    {
        v[i].col[0] = rgba[0];
        v[i].col[1] = rgba[1];
        v[i].col[2] = rgba[2];
        v[i].col[3] = rgba[3];
        v[i].texCoord[0] = 0.0f;
        v[i].texCoord[1] = 0.0f;
    }
    vk_batch.count += VK_VERTS_PER_QUAD;
}

static int vk_setup_shared_window_resources(void)
{
    if (vk_ctx.initialized)
        return 1;

    if (!create_instance())
        return 0;
    if (!pick_physical_device())
        return 0;
    if (!create_logical_device())
        return 0;

    if (!create_command_pool())
        return 0;
    if (!create_descriptor_set_layout())
        return 0;
    if (!create_descriptor_pool())
        return 0;

    vk_ctx.nextTextureId = 1;
    vk_batch.count = 0;

    LOG_INFO("Vulkan: Shared resources initialized");
    return 1;
}

static int vk_setup_separate_window_resources(size_t window_id)
{
    if (window_id >= VK_MAX_WINDOWS)
        return 0;

    if (vk_ctx.surface == VK_NULL_HANDLE)
    {
        if (!create_surface(window_id))
            return 0;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vk_ctx.physicalDevice, vk_ctx.graphicsFamily,
                                             vk_ctx.surface, &presentSupport);
        if (!presentSupport)
        {

            uint32_t qfCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(vk_ctx.physicalDevice, &qfCount, NULL);
            bool found = false;
            for (uint32_t q = 0; q < qfCount; q++)
            {
                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(vk_ctx.physicalDevice, q,
                                                     vk_ctx.surface, &supported);
                if (supported)
                {
                    vk_ctx.presentFamily = q;
                    found = true;

                    vkDestroyCommandPool(vk_ctx.device, vk_ctx.commandPool, NULL);
                    vkDestroyDevice(vk_ctx.device, NULL);
                    vk_ctx.device = VK_NULL_HANDLE;

                    float queuePriority = 1.0f;
                    VkDeviceQueueCreateInfo queueCreateInfos[2] = {
                        {
                            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                            .queueFamilyIndex = vk_ctx.graphicsFamily,
                            .queueCount = 1,
                            .pQueuePriorities = &queuePriority,
                        },
                        {
                            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                            .queueFamilyIndex = vk_ctx.presentFamily,
                            .queueCount = 1,
                            .pQueuePriorities = &queuePriority,
                        },
                    };
                    VkPhysicalDeviceFeatures deviceFeatures = {0};
                    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
                    VkDeviceCreateInfo devCreateInfo = {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                        .queueCreateInfoCount = 2,
                        .pQueueCreateInfos = queueCreateInfos,
                        .pEnabledFeatures = &deviceFeatures,
                        .enabledExtensionCount = 1,
                        .ppEnabledExtensionNames = deviceExtensions,
                    };
                    if (!vk_check_result(
                            vkCreateDevice(vk_ctx.physicalDevice, &devCreateInfo, NULL, &vk_ctx.device),
                            "vkCreateDevice (separate present queue)"))
                    {
                        return 0;
                    }
                    vkGetDeviceQueue(vk_ctx.device, vk_ctx.graphicsFamily, 0, &vk_ctx.graphicsQueue);
                    vkGetDeviceQueue(vk_ctx.device, vk_ctx.presentFamily, 0, &vk_ctx.presentQueue);

                    if (!create_command_pool())
                        return 0;
                    if (!create_descriptor_set_layout())
                        return 0;
                    if (!create_descriptor_pool())
                        return 0;
                    break;
                }
            }
            if (!found)
            {
                LOG_ERROR("Vulkan: No queue family supports presentation on this surface");
                return 0;
            }
        }

        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
        int w = 0, h = 0;
        if (platform && platform->get_window_size)
            platform->get_window_size(window_id, &w, &h);
        if (w <= 0)
            w = 800;
        if (h <= 0)
            h = 600;

        if (!create_swapchain(w, h))
            return 0;
        if (!create_render_pass())
            return 0;
        if (!create_graphics_pipelines())
            return 0;
        if (!create_framebuffers())
            return 0;
        if (!create_command_buffers())
            return 0;
        if (!create_sync_objects())
            return 0;
        if (!create_vertex_buffers())
            return 0;
        if (!create_white_texture())
            return 0;

        vk_ctx.initialized = true;
        vk_windows[window_id].fontCacheCount = 0;
    }

    LOG_INFO("Vulkan: Window %zu resources initialized", window_id);
    return 1;
}

static void vk_shutdown(void)
{
    if (!vk_ctx.initialized)
        return;

    vkDeviceWaitIdle(vk_ctx.device);

    for (int w = 0; w < VK_MAX_WINDOWS; w++)
    {
        for (int i = 0; i < vk_windows[w].fontCacheCount; i++)
            vulkan_text_renderer_cleanup(&vk_windows[w].fontCache[i].renderer);
        vk_windows[w].fontCacheCount = 0;
    }

    for (int i = 0; i < VK_MAX_TEXTURES; i++)
    {
        if (vk_ctx.textures[i].in_use)
        {
            vkDestroySampler(vk_ctx.device, vk_ctx.textures[i].sampler, NULL);
            vkDestroyImageView(vk_ctx.device, vk_ctx.textures[i].imageView, NULL);
            vkDestroyImage(vk_ctx.device, vk_ctx.textures[i].image, NULL);
            vkFreeMemory(vk_ctx.device, vk_ctx.textures[i].memory, NULL);
        }
    }

    if (vk_ctx.whiteTexture.in_use)
    {
        vkDestroySampler(vk_ctx.device, vk_ctx.whiteTexture.sampler, NULL);
        vkDestroyImageView(vk_ctx.device, vk_ctx.whiteTexture.imageView, NULL);
        vkDestroyImage(vk_ctx.device, vk_ctx.whiteTexture.image, NULL);
        vkFreeMemory(vk_ctx.device, vk_ctx.whiteTexture.memory, NULL);
    }

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(vk_ctx.device, vk_ctx.frames[i].imageAvailable, NULL);
        vkDestroySemaphore(vk_ctx.device, vk_ctx.frames[i].renderFinished, NULL);
        vkDestroyFence(vk_ctx.device, vk_ctx.frames[i].inFlight, NULL);
        if (vk_ctx.frames[i].vertexMapped)
        {
            vkUnmapMemory(vk_ctx.device, vk_ctx.frames[i].vertexMemory);
            vk_ctx.frames[i].vertexMapped = NULL;
        }
        vkDestroyBuffer(vk_ctx.device, vk_ctx.frames[i].vertexBuffer, NULL);
        vkFreeMemory(vk_ctx.device, vk_ctx.frames[i].vertexMemory, NULL);
        if (vk_ctx.frames[i].textVertexMapped)
        {
            vkUnmapMemory(vk_ctx.device, vk_ctx.frames[i].textVertexMemory);
            vk_ctx.frames[i].textVertexMapped = NULL;
        }
        vkDestroyBuffer(vk_ctx.device, vk_ctx.frames[i].textVertexBuffer, NULL);
        vkFreeMemory(vk_ctx.device, vk_ctx.frames[i].textVertexMemory, NULL);
    }

    vkDestroyCommandPool(vk_ctx.device, vk_ctx.commandPool, NULL);
    cleanup_swapchain();

    vkDestroyPipeline(vk_ctx.device, vk_ctx.shapePipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx.device, vk_ctx.shapePipelineLayout, NULL);
    vkDestroyPipeline(vk_ctx.device, vk_ctx.textPipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx.device, vk_ctx.textPipelineLayout, NULL);
    vkDestroyPipeline(vk_ctx.device, vk_ctx.imagePipeline, NULL);
    vkDestroyPipelineLayout(vk_ctx.device, vk_ctx.imagePipelineLayout, NULL);
    vkDestroyRenderPass(vk_ctx.device, vk_ctx.renderPass, NULL);
    if (vk_ctx.renderPassLoad != VK_NULL_HANDLE)
        vkDestroyRenderPass(vk_ctx.device, vk_ctx.renderPassLoad, NULL);
    vkDestroyDescriptorPool(vk_ctx.device, vk_ctx.descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(vk_ctx.device, vk_ctx.textureDescriptorLayout, NULL);
    vkDestroySurfaceKHR(vk_ctx.instance, vk_ctx.surface, NULL);
    vkDestroyDevice(vk_ctx.device, NULL);
    vkDestroyInstance(vk_ctx.instance, NULL);

    memset(&vk_ctx, 0, sizeof(vk_ctx));
    LOG_INFO("Vulkan: Shutdown complete");
}

static void vk_clear(size_t window_id, uint32_t color)
{
    vk_batch.count = 0;

    vk_convert_hex_to_rgba(vk_ctx.clearColor, color);

    if (vk_ctx.inRenderPass)
    {
        VkClearAttachment clearAtt = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue.color.float32 = {
                vk_ctx.clearColor[0], vk_ctx.clearColor[1],
                vk_ctx.clearColor[2], vk_ctx.clearColor[3]},
        };
        VkClearRect clearRect = {
            .rect = vk_ctx.partialUpdateActive
                        ? vk_ctx.currentScissor
                        : (VkRect2D){.offset = {0, 0}, .extent = vk_ctx.swapchainExtent},
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        VkCommandBuffer cmd = vk_ctx.frames[vk_ctx.currentFrame].commandBuffer;
        vkCmdClearAttachments(cmd, 1, &clearAtt, 1, &clearRect);
    }
    (void)window_id;
}

static void vk_draw_rectangle(size_t window_id, int x, int y, int width, int height)
{
    (void)window_id;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void vk_fill_rectangle(size_t window_id, int x, int y, int width, int height,
                              uint32_t color, bool isRounded, float cornerRadius)
{
    if (!isRounded)
    {
        batch_add_rect(window_id, x, y, width, height, color);
        return;
    }

    flush_shape_batch();
    if (!ensure_frame_state(window_id))
        return;

    float rgba[4];
    vk_convert_hex_to_rgba(rgba, color);

    VkVertex vertices[6];
    float x0 = (float)x, y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

    float uvs[6][2] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 6; i++)
    {
        vertices[i].col[0] = rgba[0];
        vertices[i].col[1] = rgba[1];
        vertices[i].col[2] = rgba[2];
        vertices[i].col[3] = rgba[3];
        vertices[i].texCoord[0] = uvs[i][0];
        vertices[i].texCoord[1] = uvs[i][1];
    }
    vertices[0].pos[0] = x0;
    vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1;
    vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0;
    vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1;
    vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1;
    vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0;
    vertices[5].pos[1] = y1;

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];
    VkCommandBuffer cmd = frame->commandBuffer;

    if (frame->shapeVertexOffset + 6 > VK_MAX_SHAPE_VERTICES)
        return;

    VkDeviceSize byteOff = (VkDeviceSize)frame->shapeVertexOffset * sizeof(VkVertex);
    memcpy((char *)frame->vertexMapped + byteOff, vertices, sizeof(vertices));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_ctx.shapePipeline);

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
    } pc;
    memcpy(pc.projection, vk_ctx.pushConstants.projection, sizeof(mat4x4));
    pc.size[0] = (float)width;
    pc.size[1] = (float)height;
    pc.radius = cornerRadius;
    pc.borderWidth = 1.0f;
    pc.isRounded = 1;
    pc.isHollow = 0;
    pc.shapeType = 0;
    pc.useTexture = 0;

    vkCmdPushConstants(cmd, vk_ctx.shapePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertexBuffer, &byteOff);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    frame->shapeVertexOffset += 6;
}

static void vk_draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height,
                                     uint32_t color, int border_width, bool isRounded, float cornerRadius)
{
    if (border_width <= 0)
        return;

    flush_shape_batch();
    if (!ensure_frame_state(window_id))
        return;

    float rgba[4];
    vk_convert_hex_to_rgba(rgba, color);

    VkVertex vertices[6];
    float x0 = (float)x, y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

    float uvs[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 6; i++)
    {
        vertices[i].col[0] = rgba[0];
        vertices[i].col[1] = rgba[1];
        vertices[i].col[2] = rgba[2];
        vertices[i].col[3] = rgba[3];
        vertices[i].texCoord[0] = uvs[i][0];
        vertices[i].texCoord[1] = uvs[i][1];
    }
    vertices[0].pos[0] = x0;
    vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1;
    vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0;
    vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1;
    vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1;
    vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0;
    vertices[5].pos[1] = y1;

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];
    VkCommandBuffer cmd = frame->commandBuffer;

    if (frame->shapeVertexOffset + 6 > VK_MAX_SHAPE_VERTICES)
        return;

    VkDeviceSize byteOff = (VkDeviceSize)frame->shapeVertexOffset * sizeof(VkVertex);
    memcpy((char *)frame->vertexMapped + byteOff, vertices, sizeof(vertices));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_ctx.shapePipeline);

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
    } pc;
    memcpy(pc.projection, vk_ctx.pushConstants.projection, sizeof(mat4x4));
    pc.size[0] = (float)width;
    pc.size[1] = (float)height;
    pc.radius = cornerRadius;
    pc.borderWidth = (float)border_width;
    pc.isRounded = isRounded ? 1 : 0;
    pc.isHollow = 1;
    pc.shapeType = 0;
    pc.useTexture = 0;

    vkCmdPushConstants(cmd, vk_ctx.shapePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertexBuffer, &byteOff);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    frame->shapeVertexOffset += 6;
}

static void vk_draw_arc(size_t window_id, int cx, int cy, int radius,
                        float start_angle, float end_angle, uint32_t color, int thickness)
{
    (void)window_id;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)start_angle;
    (void)end_angle;
    (void)color;
    (void)thickness;
}

static void vk_render_text(size_t window_id, AromaFont *font, const char *text,
                           int x, int y, uint32_t color, float scale)
{
    if (!font || !text || window_id >= VK_MAX_WINDOWS)
        return;

    flush_shape_batch();
    if (!ensure_frame_state(window_id))
        return;

    VulkanTextRenderer *renderer = get_or_load_font(window_id, font);
    if (!renderer)
    {
        LOG_ERROR("Vulkan: Failed to get renderer for font in window %zu", window_id);
        return;
    }

    vulkan_text_render_text(renderer, text, (float)x, (float)y, scale, color, window_id);
    vk_current_frame_number++;
}

static float vk_measure_text(size_t window_id, AromaFont *font, const char *text, float scale)
{
    if (!font || !text || window_id >= VK_MAX_WINDOWS)
        return 0.0f;

    VulkanTextRenderer *renderer = get_or_load_font(window_id, font);
    if (!renderer)
        return 0.0f;

    return vulkan_text_measure_text(renderer, text, scale);
}

void aroma_vulkan_load_font_for_window(size_t window_id, AromaFont *font)
{
    if (!font || window_id >= VK_MAX_WINDOWS)
        return;
    get_or_load_font(window_id, font);
}

static VkTextureHandle *find_texture_slot(void)
{
    for (int i = 0; i < VK_MAX_TEXTURES; i++)
    {
        if (!vk_ctx.textures[i].in_use)
            return &vk_ctx.textures[i];
    }
    return NULL;
}

static VkTextureHandle *find_texture_by_id(uint32_t id)
{
    for (int i = 0; i < VK_MAX_TEXTURES; i++)
    {
        if (vk_ctx.textures[i].in_use && vk_ctx.textures[i].id == id)
            return &vk_ctx.textures[i];
    }
    return NULL;
}

static unsigned int vk_load_image_from_rgba(unsigned char *data, int width, int height)
{
    if (!data || width <= 0 || height <= 0 || !vk_ctx.initialized)
        return 0;

    VkTextureHandle *tex = find_texture_slot();
    if (!tex)
    {
        LOG_ERROR("Vulkan: No free texture slots");
        return 0;
    }

    VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    if (!vk_create_buffer(&vk_ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &stagingBuffer, &stagingMemory))
    {
        return 0;
    }
    vk_copy_to_buffer(&vk_ctx, stagingMemory, data, imageSize);

    if (!vk_create_image(&vk_ctx, (uint32_t)width, (uint32_t)height, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         &tex->image, &tex->memory))
    {
        vkDestroyBuffer(vk_ctx.device, stagingBuffer, NULL);
        vkFreeMemory(vk_ctx.device, stagingMemory, NULL);
        return 0;
    }

    vk_transition_image_layout(&vk_ctx, tex->image,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk_copy_buffer_to_image(&vk_ctx, stagingBuffer, tex->image, (uint32_t)width, (uint32_t)height);
    vk_transition_image_layout(&vk_ctx, tex->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(vk_ctx.device, stagingBuffer, NULL);
    vkFreeMemory(vk_ctx.device, stagingMemory, NULL);

    tex->imageView = vk_create_image_view(&vk_ctx, tex->image, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_ASPECT_COLOR_BIT);
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(vk_ctx.device, &samplerInfo, NULL, &tex->sampler);

    VkDescriptorSetAllocateInfo dsAlloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_ctx.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_ctx.textureDescriptorLayout,
    };
    vkAllocateDescriptorSets(vk_ctx.device, &dsAlloc, &tex->descriptorSet);

    VkDescriptorImageInfo imgInfo = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tex->imageView,
        .sampler = tex->sampler,
    };
    VkWriteDescriptorSet dsWrite = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = tex->descriptorSet,
        .dstBinding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo = &imgInfo,
    };
    vkUpdateDescriptorSets(vk_ctx.device, 1, &dsWrite, 0, NULL);

    tex->width = width;
    tex->height = height;
    tex->id = vk_ctx.nextTextureId++;
    tex->in_use = true;

    LOG_INFO("Vulkan: Texture loaded from rgba (ID: %u, %dx%d)", tex->id, width, height);
    return tex->id;
}

static unsigned int vk_load_image_from_memory(unsigned char *data, size_t binary_length)
{
    if (!data || binary_length == 0 || !vk_ctx.initialized)
        return 0;

    stbi_set_flip_vertically_on_load(1);
    int w, h, channels;
    unsigned char *img = stbi_load_from_memory(data, (int)binary_length, &w, &h, &channels, 4);
    if (!img)
    {
        LOG_ERROR("Vulkan: Failed to decode image from memory");
        return 0;
    }

    unsigned int tex_id = vk_load_image_from_rgba(img, w, h);
    stbi_image_free(img);

    return tex_id;
}

static unsigned int vk_load_image(const char *image_path)
{
    if (!image_path || !vk_ctx.initialized)
        return 0;

    FILE *f = fopen(image_path, "rb");
    if (!f)
    {
        LOG_ERROR("Vulkan: Cannot open image: %s", image_path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)size);
    if (!data)
    {
        fclose(f);
        return 0;
    }
    fread(data, 1, (size_t)size, f);
    fclose(f);

    unsigned int result = vk_load_image_from_memory(data, (size_t)size);
    free(data);
    return result;
}

static void vk_unload_image(unsigned int texture_id)
{
    VkTextureHandle *tex = find_texture_by_id(texture_id);
    if (!tex)
        return;

    vkDeviceWaitIdle(vk_ctx.device);
    if (tex->descriptorSet) {
        vkFreeDescriptorSets(vk_ctx.device, vk_ctx.descriptorPool, 1, &tex->descriptorSet);
    }
    vkDestroySampler(vk_ctx.device, tex->sampler, NULL);
    vkDestroyImageView(vk_ctx.device, tex->imageView, NULL);
    vkDestroyImage(vk_ctx.device, tex->image, NULL);
    vkFreeMemory(vk_ctx.device, tex->memory, NULL);
    memset(tex, 0, sizeof(VkTextureHandle));
}

static void vk_draw_image(size_t window_id, int x, int y, int width, int height, unsigned int texture_id)
{
    if (texture_id == 0)
        return;

    flush_shape_batch();
    if (!ensure_frame_state(window_id))
        return;

    VkTextureHandle *tex = find_texture_by_id(texture_id);
    if (!tex)
    {
        LOG_ERROR("Vulkan: Invalid texture ID %u", texture_id);
        return;
    }

    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    VkVertex vertices[6];
    float x0 = (float)x, y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

    float uvs[6][2] = {{0, 1}, {1, 1}, {0, 0}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < 6; i++)
    {
        vertices[i].col[0] = rgba[0];
        vertices[i].col[1] = rgba[1];
        vertices[i].col[2] = rgba[2];
        vertices[i].col[3] = rgba[3];
        vertices[i].texCoord[0] = uvs[i][0];
        vertices[i].texCoord[1] = uvs[i][1];
    }
    vertices[0].pos[0] = x0;
    vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1;
    vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0;
    vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1;
    vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1;
    vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0;
    vertices[5].pos[1] = y1;

    VkFrameData *frame = &vk_ctx.frames[vk_ctx.currentFrame];
    VkCommandBuffer cmd = frame->commandBuffer;

    if (frame->shapeVertexOffset + 6 > VK_MAX_SHAPE_VERTICES)
        return;

    VkDeviceSize byteOff = (VkDeviceSize)frame->shapeVertexOffset * sizeof(VkVertex);
    memcpy((char *)frame->vertexMapped + byteOff, vertices, sizeof(vertices));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_ctx.imagePipeline);
    vkCmdPushConstants(cmd, vk_ctx.imagePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(mat4x4), vk_ctx.pushConstants.projection);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_ctx.imagePipelineLayout, 0, 1,
                            &tex->descriptorSet, 0, NULL);

    vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertexBuffer, &byteOff);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    frame->shapeVertexOffset += 6;
}

static void vk_set_clip(int x, int y, int w, int h)
{
    flush_shape_batch();

    VkRect2D scissor = {
        .offset = {x, y},
        .extent = {(uint32_t)w, (uint32_t)h},
    };

    if (!vk_ctx.inRenderPass)
    {

        vk_ctx.deferredScissor = scissor;
        vk_ctx.hasDeferredScissor = true;
        return;
    }

    VkCommandBuffer cmd = vk_ctx.frames[vk_ctx.currentFrame].commandBuffer;
    vk_ctx.currentScissor = scissor;
    vk_ctx.scissorEnabled = true;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void vk_clear_clip(void)
{
    flush_shape_batch();
    if (!vk_ctx.inRenderPass)
        return;

    VkCommandBuffer cmd = vk_ctx.frames[vk_ctx.currentFrame].commandBuffer;
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = vk_ctx.swapchainExtent,
    };
    vk_ctx.currentScissor = scissor;
    vk_ctx.scissorEnabled = false;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void vk_flush(void)
{
    flush_shape_batch();
    end_render_pass();
}

static void vk_notify_dirty_region(int x, int y, int w, int h)
{

    for (int i = 0; i < (int)vk_ctx.imageCount; i++)
    {
        if (!vk_ctx.imageDirty[i].hasDirty)
        {
            vk_ctx.imageDirty[i].x = x;
            vk_ctx.imageDirty[i].y = y;
            vk_ctx.imageDirty[i].w = w;
            vk_ctx.imageDirty[i].h = h;
            vk_ctx.imageDirty[i].hasDirty = true;
        }
        else
        {

            int ox = vk_ctx.imageDirty[i].x;
            int oy = vk_ctx.imageDirty[i].y;
            int ow = vk_ctx.imageDirty[i].w;
            int oh = vk_ctx.imageDirty[i].h;
            int nx = (x < ox) ? x : ox;
            int ny = (y < oy) ? y : oy;
            int nr = ((x + w) > (ox + ow)) ? (x + w) : (ox + ow);
            int nb = ((y + h) > (oy + oh)) ? (y + h) : (oy + oh);
            vk_ctx.imageDirty[i].x = nx;
            vk_ctx.imageDirty[i].y = ny;
            vk_ctx.imageDirty[i].w = nr - nx;
            vk_ctx.imageDirty[i].h = nb - ny;
        }
    }
}

static bool vk_get_pending_dirty_rect(int *x, int *y, int *w, int *h)
{

    for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
    {
        if (!vk_ctx.imageDirty[i].everDrawn)
            return false;
    }

    int min_x = INT32_MAX, min_y = INT32_MAX;
    int max_x = INT32_MIN, max_y = INT32_MIN;
    bool any_dirty = false;
    for (uint32_t i = 0; i < vk_ctx.imageCount; i++)
    {
        if (!vk_ctx.imageDirty[i].hasDirty)
            continue;
        any_dirty = true;
        int ix = vk_ctx.imageDirty[i].x;
        int iy = vk_ctx.imageDirty[i].y;
        int iw = vk_ctx.imageDirty[i].w;
        int ih = vk_ctx.imageDirty[i].h;
        if (ix < min_x)
            min_x = ix;
        if (iy < min_y)
            min_y = iy;
        if (ix + iw > max_x)
            max_x = ix + iw;
        if (iy + ih > max_y)
            max_y = iy + ih;
    }
    if (!any_dirty)
        return false;

    *x = min_x;
    *y = min_y;
    *w = max_x - min_x;
    *h = max_y - min_y;
    return true;
}

AromaGraphicsInterface aroma_graphics_vulkan = {
    .setup_shared_window_resources = vk_setup_shared_window_resources,
    .setup_separate_window_resources = vk_setup_separate_window_resources,
    .clear = vk_clear,
    .draw_rectangle = vk_draw_rectangle,
    .fill_rectangle = vk_fill_rectangle,
    .draw_hollow_rectangle = vk_draw_hollow_rectangle,
    .draw_arc = vk_draw_arc,
    .render_text = vk_render_text,
    .measure_text = vk_measure_text,
    .unload_image = vk_unload_image,
    .load_image = vk_load_image,
    .load_image_from_rgba = vk_load_image_from_rgba,
    .load_image_from_memory = vk_load_image_from_memory,
    .draw_image = vk_draw_image,
    .shutdown = vk_shutdown,
    .graphics_set_clip = vk_set_clip,
    .graphics_clear_clip = vk_clear_clip,
    .graphics_flush = vk_flush,
    .notify_dirty_region = vk_notify_dirty_region,
    .get_pending_dirty_rect = vk_get_pending_dirty_rect,
};

#endif
