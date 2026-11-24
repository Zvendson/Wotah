/**
 * @file app.cpp
 * The application: a minimal Vulkan 1.3 "hello square" that draws one orange square in the center of a window.
 * Uses dynamic rendering (no VkRenderPass / VkFramebuffer) and no vertex buffer.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "render/pipeline.hpp"
#include "render/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

/** The square's vertex shader (shaders/square.vert), compiled to SPIR-V by the build. */
static constexpr uint32_t SquareVertexShader[] = {
#include "square.vert.inc"
};

/** The square's fragment shader (shaders/square.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t SquareFragmentShader[] = {
#include "square.frag.inc"
};

#define VK_CHECK(call)                                                                       \
    do                                                                                       \
    {                                                                                        \
        VkResult r_ = (call);                                                                \
        if (r_ != VK_SUCCESS)                                                                \
        {                                                                                    \
            Wotah::Logger::Critical("{} failed (VkResult {})", #call, static_cast<int>(r_)); \
            Wotah::Logger::Destroy(); /* writes the line out before the exit */              \
            std::exit(1);                                                                    \
        }                                                                                    \
    } while (0)

static void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                       VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.oldLayout           = from;
    b.newLayout           = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

/** The window's size: square, so the [-0.5, 0.5] quad in the shader is a real square on screen. */
static constexpr Wotah::WindowSize SquareWindowSize{600, 600};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Square";

/**
 * The Vulkan objects the application creates on top of the device, in creation order; null handles mean "not created
 * (yet)".
 */
struct Wotah::App::State
{
    /** The swapchain, its images and one view per image. */
    Wotah::Swapchain Swapchain;

    /** The pipeline that draws the square. */
    Wotah::Pipeline Pipeline;

    /** One command buffer and its sync objects: one frame in flight. */
    VkCommandPool            CommandPool    = VK_NULL_HANDLE;
    VkCommandBuffer          CommandBuffer  = VK_NULL_HANDLE;
    VkSemaphore              ImageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> RenderFinished; // one per swapchain image
    VkFence                  FrameFence = VK_NULL_HANDLE;
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

    // ---- Instance, surface, GPU and device ------------------------------------
    if (!m_device.Create(m_window, AppName))
    {
        return false;
    }
    const VkDevice device = m_device.GetHandle();

    // ---- Swapchain -----------------------------------------------------------
    if (!state.Swapchain.Create(m_device, m_window))
    {
        return false;
    }
    const uint32_t imageCount = state.Swapchain.GetImageCount();

    // ---- Pipeline ------------------------------------------------------------
    // The square's shaders make their own six vertices, so every other setting keeps its default.
    PipelineSettings squareSettings;
    squareSettings.VertexShader   = SquareVertexShader;
    squareSettings.FragmentShader = SquareFragmentShader;
    squareSettings.ColorFormat    = state.Swapchain.GetFormat();
    if (!state.Pipeline.Create(m_device, squareSettings))
    {
        return false;
    }

    // ---- Command buffer + sync -----------------------------------------------
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = m_device.GetQueueFamily();
    VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &state.CommandPool));

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = state.CommandPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &state.CommandBuffer));

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &state.ImageAvailable));
    state.RenderFinished.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkSemaphore semaphore;
        VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &semaphore));
        state.RenderFinished.push_back(semaphore);
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &state.FrameFence));
    return true;
}

void Wotah::App::Start()
{
    m_window.Show();
}

void Wotah::App::Run()
{
    State&          state  = *m_state;
    const VkDevice  device = m_device.GetHandle();
    const VkQueue   queue  = m_device.GetQueue();
    VkCommandBuffer cmd    = state.CommandBuffer;

    // Fixed while the window cannot be resized.
    const VkExtent2D extent = state.Swapchain.GetExtent();

    Start();

    // One frame in flight.
    while (!m_window.ShouldClose())
    {
        m_window.PollEvents();
        if (m_window.ShouldClose()) // closed while minimized: no frame for a window that is going away
        {
            break;
        }

        vkWaitForFences(device, 1, &state.FrameFence, VK_TRUE, UINT64_MAX);

        const std::optional<uint32_t> acquired = state.Swapchain.AcquireNextImage(state.ImageAvailable);
        if (!acquired)
        {
            continue;
        }
        const uint32_t idx = *acquired;
        vkResetFences(device, 1, &state.FrameFence);

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        transition(cmd, state.Swapchain.GetImage(idx), VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView        = state.Swapchain.GetView(idx);
        color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // dark gray background

        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea           = {{0, 0}, extent};
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &color;

        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.Pipeline.GetHandle());
        VkViewport viewport{0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
        VkRect2D   scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDraw(cmd, 6, 1, 0, 0); // 6 vertices = 2 triangles = 1 square
        vkCmdEndRendering(cmd);

        transition(cmd, state.Swapchain.GetImage(idx), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &state.ImageAvailable;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &state.RenderFinished[idx];
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, state.FrameFence));

        state.Swapchain.Present(queue, state.RenderFinished[idx], idx);
    }

    Stop();
}

void Wotah::App::Stop()
{
    m_device.WaitUntilIdle();
}

void Wotah::App::Destroy()
{
    State&         state  = *m_state;
    const VkDevice device = m_device.GetHandle();

    // Children before their parents: the objects made on the device, the device, the window, then the log.
    // vkDestroy* accepts VK_NULL_HANDLE (Vulkan spec, "Object Lifetime"), so a part-way Create is fine.
    if (device != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, state.FrameFence, nullptr);
        for (VkSemaphore semaphore : state.RenderFinished)
        {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        vkDestroySemaphore(device, state.ImageAvailable, nullptr);
        vkDestroyCommandPool(device, state.CommandPool, nullptr); // frees the command buffer too
    }
    state.Pipeline.Destroy();
    state.Swapchain.Destroy();
    // Everything made on the device is gone; a fresh State forgets the handles, so a second Destroy does nothing.
    m_state = std::make_unique<State>();

    m_device.Destroy();
    m_window.Destroy();
    Logger::Destroy(); // last, so everything above can still log
}
