/**
 * @file app.cpp
 * The application: a minimal Vulkan 1.3 "hello square" that draws one orange square in the center of a window.
 * Uses dynamic rendering (no VkRenderPass / VkFramebuffer) and no vertex buffer.
 */
#include "app.hpp"

#include "logger.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

static const uint32_t kVertSpv[] = {
#include "square.vert.inc"
};
static const uint32_t kFragSpv[] = {
#include "square.frag.inc"
};

#define VK_CHECK(call)                                                              \
    do {                                                                            \
        VkResult r_ = (call);                                                       \
        if (r_ != VK_SUCCESS) {                                                     \
            Wotah::Logger::Critical("{} failed (VkResult {})", #call, static_cast<int>(r_)); \
            Wotah::Logger::Destroy(); /* writes the line out before the exit */     \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

static VkShaderModule createShader(VkDevice device, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &module));
    return module;
}

static void transition(VkCommandBuffer cmd, VkImage image,
                       VkImageLayout from, VkImageLayout to,
                       VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

/** The window's size: square, so the [-0.5, 0.5] quad in the shader is a real square on screen. */
static constexpr Wotah::WindowSize SquareWindowSize{600, 600};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Square";

/** Every Vulkan object the application creates, in creation order; null handles mean "not created (yet)". */
struct Wotah::App::State
{
    /** The Vulkan instance and the window's surface. */
    VkInstance Instance = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;

    /** The chosen GPU and its queue family that can both draw and present. */
    VkPhysicalDevice Gpu = VK_NULL_HANDLE;
    uint32_t QueueFamily = 0;

    /** The logical device and its one queue. */
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;

    /** The swapchain, its images and one view per image. */
    VkSurfaceFormatKHR Format{};
    VkExtent2D Extent{};
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> Images;
    std::vector<VkImageView> Views;

    /** The pipeline that draws the square. */
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;

    /** One command buffer and its sync objects: one frame in flight. */
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkSemaphore ImageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> RenderFinished; // one per swapchain image
    VkFence FrameFence = VK_NULL_HANDLE;
};

Wotah::App::App()
    : m_state(std::make_unique<State>())
{
}

Wotah::App::~App()
{
    Stop();
    Destroy();
}

bool Wotah::App::Create()
{
    State& state = *m_state;

    // First, so every later step can log.
    Logger::Create();

    // ---- Window --------------------------------------------------------------
    if (!m_window.Create(SquareWindowSize, AppName))
    {
        return false;
    }

    // ---- Instance ------------------------------------------------------------
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = AppName;
    app.apiVersion = VK_API_VERSION_1_3;

    const std::span<const char* const> windowExtensions = m_window.GetRequiredInstanceExtensions();
    if (windowExtensions.empty())
    {
        return false;
    }

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(windowExtensions.size()); // GLFW gave it as a uint32_t
    ici.ppEnabledExtensionNames = windowExtensions.data();

#ifndef NDEBUG
    // Enable validation in debug builds if the SDK's layer is installed.
    const char* validation = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    for (const auto& l : layers)
    {
        if (std::strcmp(l.layerName, validation) == 0)
        {
            ici.enabledLayerCount = 1;
            ici.ppEnabledLayerNames = &validation;
        }
    }
#endif

    VK_CHECK(vkCreateInstance(&ici, nullptr, &state.Instance));
    state.Surface = m_window.CreateSurface(state.Instance);
    if (state.Surface == VK_NULL_HANDLE)
    {
        return false;
    }

    // ---- Pick a GPU + a queue family that can draw and present ---------------
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(state.Instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(state.Instance, &gpuCount, gpus.data());

    for (VkPhysicalDevice d : gpus)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_3)
        {
            continue;
        }

        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, qfs.data());
        for (uint32_t i = 0; i < qfCount && state.Gpu == VK_NULL_HANDLE; ++i)
        {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, state.Surface, &canPresent);
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent)
            {
                state.Gpu = d;
                state.QueueFamily = i;
            }
        }
        if (state.Gpu != VK_NULL_HANDLE)
        {
            break;
        }
    }
    if (state.Gpu == VK_NULL_HANDLE)
    {
        Logger::Error("No Vulkan 1.3 GPU with graphics + present support found");
        return false;
    }
    VkPhysicalDeviceProperties gpuProperties;
    vkGetPhysicalDeviceProperties(state.Gpu, &gpuProperties);
    Logger::Info("Using GPU {}", gpuProperties.deviceName);

    // ---- Logical device ------------------------------------------------------
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = state.QueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;

    const char* deviceExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &features13;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &deviceExt;

    VK_CHECK(vkCreateDevice(state.Gpu, &dci, nullptr, &state.Device));
    vkGetDeviceQueue(state.Device, state.QueueFamily, 0, &state.Queue);

    // ---- Swapchain -----------------------------------------------------------
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.Gpu, state.Surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(state.Gpu, state.Surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(state.Gpu, state.Surface, &fmtCount, formats.data());
    state.Format = formats[0];

    state.Extent = caps.currentExtent;
    if (state.Extent.width == UINT32_MAX) // e.g. Wayland: surface size is chosen by us
    {
        const WindowSize framebufferSize = m_window.GetFramebufferSize();
        state.Extent = {framebufferSize.Width, framebufferSize.Height};
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = state.Surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = state.Format.format;
    sci.imageColorSpace = state.Format.colorSpace;
    sci.imageExtent = state.Extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync, always supported
    sci.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(state.Device, &sci, nullptr, &state.Swapchain));

    vkGetSwapchainImagesKHR(state.Device, state.Swapchain, &imageCount, nullptr);
    state.Images.resize(imageCount);
    vkGetSwapchainImagesKHR(state.Device, state.Swapchain, &imageCount, state.Images.data());

    // Views are created one by one, so a failure part way leaves only real handles behind for Destroy.
    state.Views.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = state.Images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = state.Format.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        VK_CHECK(vkCreateImageView(state.Device, &vci, nullptr, &view));
        state.Views.push_back(view);
    }

    // ---- Pipeline ------------------------------------------------------------
    VkShaderModule vert = createShader(state.Device, kVertSpv, sizeof(kVertSpv));
    VkShaderModule frag = createShader(state.Device, kFragSpv, sizeof(kFragSpv));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
    };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VK_CHECK(vkCreatePipelineLayout(state.Device, &lci, nullptr, &state.PipelineLayout));

    // Dynamic rendering: tell the pipeline the attachment format instead of a render pass.
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &state.Format.format;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &renderingInfo;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &multisample;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = state.PipelineLayout;

    VK_CHECK(vkCreateGraphicsPipelines(state.Device, VK_NULL_HANDLE, 1, &pci, nullptr, &state.Pipeline));
    vkDestroyShaderModule(state.Device, vert, nullptr);
    vkDestroyShaderModule(state.Device, frag, nullptr);

    // ---- Command buffer + sync -----------------------------------------------
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = state.QueueFamily;
    VK_CHECK(vkCreateCommandPool(state.Device, &cpci, nullptr, &state.CommandPool));

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = state.CommandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(state.Device, &cbai, &state.CommandBuffer));

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(state.Device, &semInfo, nullptr, &state.ImageAvailable));
    state.RenderFinished.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkSemaphore semaphore;
        VK_CHECK(vkCreateSemaphore(state.Device, &semInfo, nullptr, &semaphore));
        state.RenderFinished.push_back(semaphore);
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(state.Device, &fenceInfo, nullptr, &state.FrameFence));
    return true;
}

void Wotah::App::Start()
{
    m_window.Show();
}

void Wotah::App::Run()
{
    State& state = *m_state;
    VkCommandBuffer cmd = state.CommandBuffer;

    Start();

    // One frame in flight.
    while (!m_window.ShouldClose())
    {
        m_window.PollEvents();
        if (m_window.ShouldClose()) // closed while minimized: no frame for a window that is going away
        {
            break;
        }

        vkWaitForFences(state.Device, 1, &state.FrameFence, VK_TRUE, UINT64_MAX);

        uint32_t idx;
        VkResult acq = vkAcquireNextImageKHR(state.Device, state.Swapchain, UINT64_MAX, state.ImageAvailable,
                                             VK_NULL_HANDLE, &idx);
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        {
            continue;
        }
        vkResetFences(state.Device, 1, &state.FrameFence);

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        transition(cmd, state.Images[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = state.Views[idx];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // dark gray background

        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, state.Extent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;

        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.Pipeline);
        VkViewport viewport{0.0f, 0.0f, (float)state.Extent.width, (float)state.Extent.height, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, state.Extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDraw(cmd, 6, 1, 0, 0); // 6 vertices = 2 triangles = 1 square
        vkCmdEndRendering(cmd);

        transition(cmd, state.Images[idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &state.ImageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &state.RenderFinished[idx];
        VK_CHECK(vkQueueSubmit(state.Queue, 1, &submit, state.FrameFence));

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &state.RenderFinished[idx];
        present.swapchainCount = 1;
        present.pSwapchains = &state.Swapchain;
        present.pImageIndices = &idx;
        vkQueuePresentKHR(state.Queue, &present);
    }

    Stop();
}

void Wotah::App::Stop()
{
    if (m_state->Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_state->Device);
    }
}

void Wotah::App::Destroy()
{
    State& state = *m_state;

    // Children before their parents: the device's objects, the device, the surface, the instance, the window.
    // vkDestroy* accepts VK_NULL_HANDLE (Vulkan spec, "Object Lifetime"), so a part-way Create is fine.
    if (state.Device != VK_NULL_HANDLE)
    {
        vkDestroyFence(state.Device, state.FrameFence, nullptr);
        for (VkSemaphore semaphore : state.RenderFinished)
        {
            vkDestroySemaphore(state.Device, semaphore, nullptr);
        }
        vkDestroySemaphore(state.Device, state.ImageAvailable, nullptr);
        vkDestroyCommandPool(state.Device, state.CommandPool, nullptr); // frees the command buffer too
        vkDestroyPipeline(state.Device, state.Pipeline, nullptr);
        vkDestroyPipelineLayout(state.Device, state.PipelineLayout, nullptr);
        for (VkImageView view : state.Views)
        {
            vkDestroyImageView(state.Device, view, nullptr);
        }
        vkDestroySwapchainKHR(state.Device, state.Swapchain, nullptr);
        vkDestroyDevice(state.Device, nullptr);
    }
    if (state.Instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(state.Instance, state.Surface, nullptr);
        vkDestroyInstance(state.Instance, nullptr);
    }
    m_window.Destroy();
    Logger::Destroy(); // last, so everything above can still log

    // Everything is gone; forget the handles so a second Destroy does nothing.
    state = State{};
}
