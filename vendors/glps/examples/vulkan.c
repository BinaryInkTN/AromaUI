#define GLPS_USE_VULKAN
#include <GLPS/glps_window_manager.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VK_CHECK(x)                                                                 \
    do                                                                              \
    {                                                                               \
        VkResult err = x;                                                           \
        if (err != VK_SUCCESS)                                                      \
        {                                                                           \
            fprintf(stderr, "Vulkan error %d at %s:%d\n", err, __FILE__, __LINE__); \
            exit(1);                                                                \
        }                                                                           \
    } while (0)

typedef struct
{
    float x, y, z;
    float r, g, b, a;
} Vertex;

typedef struct
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t vertex_count;
} PrimitiveBuffer;

static uint32_t *read_spirv_file(const char *filename, size_t *size)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        fprintf(stderr, "Failed to open shader file: %s\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (*size % 4 != 0)
    {
        fprintf(stderr, "SPIR-V size not multiple of 4: %s\n", filename);
        fclose(file);
        return NULL;
    }
    uint32_t *code = (uint32_t *)malloc(*size);
    if (!code)
    {
        fclose(file);
        return NULL;
    }
    if (fread(code, 1, *size, file) != *size)
    {
        fprintf(stderr, "Failed to read shader: %s\n", filename);
        free(code);
        fclose(file);
        return NULL;
    }
    fclose(file);
    return code;
}

VkShaderModule create_shader_module(VkDevice device, const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code};
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &info, NULL, &module));
    return module;
}

uint32_t find_memory_type(VkPhysicalDevice gpu, uint32_t type_filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
    {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    fprintf(stderr, "Failed to find suitable memory type!\n");
    exit(1);
}

PrimitiveBuffer create_rectangle(VkDevice device, VkPhysicalDevice gpu, float x, float y, float w, float h, float r, float g, float b, float a, int win_w, int win_h)
{
    float x1 = (x / (float)win_w) * 2.0f - 1.0f;
    float y1 = (y / (float)win_h) * 2.0f - 1.0f;
    float x2 = ((x + w) / (float)win_w) * 2.0f - 1.0f;
    float y2 = ((y + h) / (float)win_h) * 2.0f - 1.0f;

    Vertex vertices[6] = {
        {x1, y1, 0, r, g, b, a},
        {x2, y1, 0, r, g, b, a},
        {x2, y2, 0, r, g, b, a},
        {x1, y1, 0, r, g, b, a},
        {x2, y2, 0, r, g, b, a},
        {x1, y2, 0, r, g, b, a}};

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(vertices),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    PrimitiveBuffer pb = {0};
    pb.vertex_count = 6;
    VK_CHECK(vkCreateBuffer(device, &buf_info, NULL, &pb.buffer));

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device, pb.buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = find_memory_type(gpu, mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, &pb.memory));
    VK_CHECK(vkBindBufferMemory(device, pb.buffer, pb.memory, 0));

    void *data;
    vkMapMemory(device, pb.memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, pb.memory);

    return pb;
}

void destroy_primitive_buffer(VkDevice device, PrimitiveBuffer *pb)
{
    if (pb->buffer)
        vkDestroyBuffer(device, pb->buffer, NULL);
    if (pb->memory)
        vkFreeMemory(device, pb->memory, NULL);
    memset(pb, 0, sizeof(PrimitiveBuffer));
}

void draw_primitive(VkCommandBuffer cmd, PrimitiveBuffer *pb)
{
    VkBuffer vb[] = {pb->buffer};
    VkDeviceSize offs[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vb, offs);
    vkCmdDraw(cmd, pb->vertex_count, 1, 0, 0);
}

int main()
{
    size_t vert_size, frag_size;
    uint32_t *vert_spv = read_spirv_file("./vert.spv", &vert_size);
    uint32_t *frag_spv = read_spirv_file("./frag.spv", &frag_size);
    if (!vert_spv || !frag_spv)
    {
        fprintf(stderr, "Failed shaders\n");
        return 1;
    }

    glps_WindowManager *wm = glps_wm_init();
    size_t window = glps_wm_window_create(wm, "Vulkan Primitives", 800, 600);

    // Vulkan instance
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Primitives", .apiVersion = VK_API_VERSION_1_0};
    glps_VulkanExtensionArray exts_arr = glps_wm_vk_get_extensions_arr();
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = exts_arr.count,
        .ppEnabledExtensionNames = exts_arr.names};
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&inst_info, NULL, &instance));

    VkSurfaceKHR surface;
    glps_wm_vk_create_surface(wm, window, &instance, &surface);
    printf("✅ Vulkan Xlib surface created!\n");

    // Pick GPU
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
    if (gpu_count == 0)
    {
        fprintf(stderr, "No GPU found!\n");
        return 1;
    }
    VkPhysicalDevice devices[8];
    vkEnumeratePhysicalDevices(instance, &gpu_count, devices);
    VkPhysicalDevice gpu = devices[0];

    // Queue
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, NULL);
    VkQueueFamilyProperties qprops[32];
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, qprops);
    int qidx = -1;
    for (uint32_t i = 0; i < qcount; i++)
    {
        VkBool32 pres = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &pres);
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres)
        {
            qidx = i;
            break;
        }
    }
    if (qidx == -1)
    {
        fprintf(stderr, "No queue found!\n");
        return 1;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qidx, .queueCount = 1, .pQueuePriorities = &priority};
    const char *dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dinfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qinfo, .enabledExtensionCount = 1, .ppEnabledExtensionNames = dev_exts};
    VkDevice device;
    VK_CHECK(vkCreateDevice(gpu, &dinfo, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, qidx, 0, &queue);

    // Swapchain
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    VkExtent2D extent = caps.currentExtent.width == UINT32_MAX ? (VkExtent2D){800, 600} : caps.currentExtent;
    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    uint32_t fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_count, NULL);
    VkSurfaceFormatKHR formats[32];
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_count, formats);
    VkSurfaceFormatKHR fmt = formats[0];

    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = img_count,
        .imageFormat = fmt.format,
        .imageColorSpace = fmt.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE};
    VkSwapchainKHR swapchain;
    VK_CHECK(vkCreateSwapchainKHR(device, &swap_info, NULL, &swapchain));

    uint32_t real_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &real_count, NULL);
    VkImage imgs[8];
    vkGetSwapchainImagesKHR(device, swapchain, &real_count, imgs);

    VkImageView views[8];
    for (uint32_t i = 0; i < real_count; i++)
    {
        VkImageViewCreateInfo iv = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = imgs[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt.format, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VK_CHECK(vkCreateImageView(device, &iv, NULL, &views[i]));
    }

    // Render pass
    VkAttachmentDescription color_attachment = {.format = fmt.format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color_ref};
    VkRenderPassCreateInfo rp_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color_attachment, .subpassCount = 1, .pSubpasses = &subpass};
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &rp_info, NULL, &render_pass));

    // Framebuffers
    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < real_count; i++)
    {
        VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = render_pass, .attachmentCount = 1, .pAttachments = &views[i], .width = extent.width, .height = extent.height, .layers = 1};
        VK_CHECK(vkCreateFramebuffer(device, &fb, NULL, &framebuffers[i]));
    }

    // Shaders
    VkShaderModule vert_module = create_shader_module(device, vert_spv, vert_size);
    VkShaderModule frag_module = create_shader_module(device, frag_spv, frag_size);
    free(vert_spv);
    free(frag_spv);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main"}};

    VkVertexInputBindingDescription binding = {.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, x)}, {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, r)}};
    VkPipelineVertexInputStateCreateInfo vi_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE};

    VkViewport vp = {.x = 0, .y = 0, .width = (float)extent.width, .height = (float)extent.height, .minDepth = 0, .maxDepth = 1};
    VkRect2D scissor = {{0, 0}, extent};
    VkPipelineViewportStateCreateInfo vp_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &scissor};

    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_CLOCKWISE, .depthBiasEnable = VK_FALSE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .sampleShadingEnable = VK_FALSE};
    VkPipelineColorBlendAttachmentState cb_attach = {.blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .logicOpEnable = VK_FALSE, .attachmentCount = 1, .pAttachments = &cb_attach};

    VkPipelineLayout pl;
    VkPipelineLayoutCreateInfo pl_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VK_CHECK(vkCreatePipelineLayout(device, &pl_info, NULL, &pl));
    VkGraphicsPipelineCreateInfo pipe_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vi_info, .pInputAssemblyState = &ia, .pViewportState = &vp_info, .pRasterizationState = &raster, .pMultisampleState = &ms, .pDepthStencilState = NULL, .pColorBlendState = &cb, .pDynamicState = NULL, .layout = pl, .renderPass = render_pass, .subpass = 0, .basePipelineHandle = VK_NULL_HANDLE, .basePipelineIndex = -1};
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline));

    // Primitives
    PrimitiveBuffer red = create_rectangle(device, gpu, 100, 100, 200, 150, 1, 0, 0, 1, extent.width, extent.height);
    PrimitiveBuffer green = create_rectangle(device, gpu, 400, 200, 150, 100, 0, 1, 0, 1, extent.width, extent.height);
    PrimitiveBuffer blue = create_rectangle(device, gpu, 250, 350, 300, 120, 0, 0, 1, 1, extent.width, extent.height);

    // Command pool + buffers
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qidx};
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = real_count};
    VkCommandBuffer *cmds = malloc(sizeof(VkCommandBuffer) * real_count);
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, cmds));

    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore img_avail, render_done;
    VK_CHECK(vkCreateSemaphore(device, &sem_info, NULL, &img_avail));
    VK_CHECK(vkCreateSemaphore(device, &sem_info, NULL, &render_done));

    // Main loop
    while (!glps_wm_should_close(wm))
    {
        uint32_t img_idx;
        VK_CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, img_avail, VK_NULL_HANDLE, &img_idx));

        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cmds[img_idx], &begin));
        VkClearValue clear = {.color = {0.1f, 0.1f, 0.1f, 1.0f}};
        VkRenderPassBeginInfo rpbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render_pass, .framebuffer = framebuffers[img_idx], .renderArea = {{0, 0}, extent}, .clearValueCount = 1, .pClearValues = &clear};
        vkCmdBeginRenderPass(cmds[img_idx], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmds[img_idx], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        draw_primitive(cmds[img_idx], &red);
        draw_primitive(cmds[img_idx], &green);
        draw_primitive(cmds[img_idx], &blue);
        vkCmdEndRenderPass(cmds[img_idx]);
        VK_CHECK(vkEndCommandBuffer(cmds[img_idx]));

        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &img_avail, .pWaitDstStageMask = (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}, .commandBufferCount = 1, .pCommandBuffers = &cmds[img_idx], .signalSemaphoreCount = 1, .pSignalSemaphores = &render_done};
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));

        VkPresentInfoKHR pres = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &render_done, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &img_idx};
        VK_CHECK(vkQueuePresentKHR(queue, &pres));
        vkQueueWaitIdle(queue);
    }

    vkDeviceWaitIdle(device);

    destroy_primitive_buffer(device, &red);
    destroy_primitive_buffer(device, &green);
    destroy_primitive_buffer(device, &blue);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pl, NULL);
    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);
    for (uint32_t i = 0; i < real_count; i++)
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    for (uint32_t i = 0; i < real_count; i++)
        vkDestroyImageView(device, views[i], NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroySemaphore(device, img_avail, NULL);
    vkDestroySemaphore(device, render_done, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    glps_wm_destroy(wm);
    free(cmds);

    return 0;
}
