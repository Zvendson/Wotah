/**
 * @file app.cpp
 * The application: a minimal Vulkan 1.3 "hello square" that draws one orange square in the center of a window.
 * Uses dynamic rendering (no VkRenderPass / VkFramebuffer) and no vertex buffer.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

/** The square's vertex shader (shaders/square.vert), compiled to SPIR-V by the build. */
static constexpr uint32_t SquareVertexShader[] = {
#include "square.vert.inc"
};

/** The square's fragment shader (shaders/square.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t SquareFragmentShader[] = {
#include "square.frag.inc"
};

/** The square's vertices: two triangles, made by the vertex shader itself. */
static constexpr uint32_t SquareVertexCount = 6;

/** The window's size: square, so the [-0.5, 0.5] quad in the shader is a real square on screen. */
static constexpr Wotah::WindowSize SquareWindowSize{600, 600};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Square";

/** The background: dark gray. */
static constexpr VkClearColorValue BackgroundColor{{0.1f, 0.1f, 0.1f, 1.0f}};

/**
 * The Vulkan objects the application creates on top of the device. Declared in creation order, so they are destroyed
 * in reverse: the renderer before the swapchain it draws into.
 */
struct Wotah::App::State
{
    /** The swapchain, its images and one view per image. */
    Wotah::Swapchain Swapchain;

    /** The pipeline that draws the square. */
    Wotah::Pipeline Pipeline;

    /** The command buffer and synchronization of each frame. */
    Wotah::Renderer Renderer;
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

    if (!m_window.Create(SquareWindowSize, AppName) || !m_device.Create(m_window, AppName) ||
        !state.Swapchain.Create(m_device, m_window))
    {
        return false;
    }

    // The square's shaders make their own vertices, so every other setting keeps its default.
    PipelineSettings squareSettings;
    squareSettings.VertexShader   = SquareVertexShader;
    squareSettings.FragmentShader = SquareFragmentShader;
    squareSettings.ColorFormat    = state.Swapchain.GetFormat();
    if (!state.Pipeline.Create(m_device, squareSettings))
    {
        return false;
    }

    if (!state.Renderer.Create(m_device, state.Swapchain))
    {
        return false;
    }
    state.Renderer.SetClearColor(BackgroundColor);
    return true;
}

bool Wotah::App::Run()
{
    Renderer&       renderer = m_state->Renderer;
    const Pipeline& pipeline = m_state->Pipeline;

    Start();

    bool cleanEnd = true;
    while (!m_window.ShouldClose())
    {
        m_window.PollEvents();
        if (m_window.ShouldClose()) // closed while minimized: no frame for a window that is going away
        {
            break;
        }

        const FrameStart start = renderer.BeginFrame();
        if (start == FrameStart::Skip)
        {
            continue;
        }
        if (start == FrameStart::Failed)
        {
            cleanEnd = false;
            break;
        }

        const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
        vkCmdDraw(commandBuffer, SquareVertexCount, 1, 0, 0);

        if (!renderer.EndFrame())
        {
            cleanEnd = false;
            break;
        }
    }

    if (!cleanEnd)
    {
        Logger::Critical("Stopped drawing after a Vulkan error");
    }
    Stop();
    return cleanEnd;
}

void Wotah::App::Start()
{
    m_window.Show();
}

void Wotah::App::Stop()
{
    m_device.WaitUntilIdle();
}

void Wotah::App::Destroy()
{
    // Children before their parents: the objects made on the device (a fresh State destroys the old one, renderer
    // first), the device, the window, then the log.
    m_state = std::make_unique<State>();
    m_device.Destroy();
    m_window.Destroy();
    Logger::Destroy(); // last, so everything above can still log
}
