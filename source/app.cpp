/**
 * @file app.cpp
 * The application: a Vulkan 1.3 example that draws an orange cube, seen from above one corner so three faces show.
 * Uses dynamic rendering (no VkRenderPass / VkFramebuffer); the cube's vertices are made by its vertex shader, and the
 * camera comes in as a push constant.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "input/input_event.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/swapchain.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

/** The clock frame times are measured with: steady, so changing the system time cannot make a frame negative. */
using Clock = std::chrono::steady_clock;

/**
 * The longest frame time the camera moves by: after a stall (dragging the window, a breakpoint) it continues from where
 * it was instead of jumping far.
 */
static constexpr float MaxFrameSeconds = 0.1f;

/** The cube's vertex shader (shaders/cube.vert), compiled to SPIR-V by the build. */
static constexpr uint32_t CubeVertexShader[] = {
#include "cube.vert.inc"
};

/** The cube's fragment shader (shaders/cube.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
};

/** The cube's vertices: 6 faces x 2 triangles x 3 corners, made by the vertex shader itself. */
static constexpr uint32_t CubeVertexCount = 36;

/** The window's size in pixels. */
static constexpr Wotah::WindowSize MainWindowSize{600, 600};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Cube";

/** The background: dark gray. */
static constexpr VkClearColorValue BackgroundColor{{0.1f, 0.1f, 0.1f, 1.0f}};

/** Where the first camera stands: above the corner where +x, +y and +z meet, so those three faces show. */
static constexpr glm::vec3 CornerCameraPosition{2.0f, 1.6f, 2.6f};

/** What the first camera looks at: the cube's center. */
static constexpr glm::vec3 CornerCameraTarget{0.0f, 0.0f, 0.0f};

/** The nearest and farthest distance the first camera sees: the cube is about 3.7 units away. */
static constexpr float CornerCameraNear = 0.1f;
static constexpr float CornerCameraFar  = 10.0f;

/** What the cube's vertex shader gets per draw; matches PushConstants in shaders/cube.vert. */
struct CubePushConstants
{
    /** World space to clip space: the active camera. */
    glm::mat4 ViewProjection{1.0f};
};

/** The one push constant range: the cube's camera, read by the vertex shader. */
static constexpr std::array<VkPushConstantRange, 1> CubePushConstantRanges{
    {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CubePushConstants)}}};

/**
 * The Vulkan objects the application creates on top of the device. Declared in creation order, so they are destroyed
 * in reverse: the renderer before the swapchain it draws into.
 */
struct Wotah::App::State
{
    /** The swapchain, its images and one view per image. */
    Wotah::Swapchain Swapchain;

    /** The pipeline that draws the cube. */
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

    if (!m_window.Create(MainWindowSize, AppName) || !m_device.Create(m_window, AppName) ||
        !state.Swapchain.Create(m_device, m_window))
    {
        return false;
    }

    // The cube's shader makes its own vertices. Its faces are counter-clockwise seen from outside, so culling the back
    // faces is enough to hide the three faces turned away; one convex cube needs no depth image.
    PipelineSettings cubeSettings;
    cubeSettings.VertexShader   = CubeVertexShader;
    cubeSettings.FragmentShader = CubeFragmentShader;
    cubeSettings.ColorFormat    = state.Swapchain.GetFormat();
    cubeSettings.CullMode       = VK_CULL_MODE_BACK_BIT;
    cubeSettings.FrontFace      = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    cubeSettings.PushConstants  = CubePushConstantRanges;
    if (!state.Pipeline.Create(m_device, cubeSettings))
    {
        return false;
    }

    // The first camera looks at the cube from above a corner; Run keeps every active camera's aspect ratio in step
    // with the image.
    Camera& corner = m_cameras.front();
    corner.SetPosition(CornerCameraPosition);
    corner.SetTarget(CornerCameraTarget);
    if (!corner.SetClipDistances(CornerCameraNear, CornerCameraFar))
    {
        return false;
    }
    m_focusViews.front() = corner;

    if (!state.Renderer.Create(m_device, state.Swapchain))
    {
        return false;
    }
    state.Renderer.SetClearColor(BackgroundColor);

    // The camera controller gets whatever input no handler before it used; a UI will be added in front of it.
    m_input.AddHandler(m_cameraController);
    return true;
}

bool Wotah::App::Run()
{
    Renderer&       renderer = m_state->Renderer;
    const Pipeline& pipeline = m_state->Pipeline;

    // Fixed while the window cannot be resized.
    const VkExtent2D extent      = m_state->Swapchain.GetExtent();
    const float      aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    Start();

    bool              cleanEnd  = true;
    Clock::time_point lastFrame = Clock::now();
    while (!m_window.ShouldClose())
    {
        m_window.PollEvents();
        if (m_window.ShouldClose()) // closed while minimized: no frame for a window that is going away
        {
            break;
        }

        // ---- Input: hand it out, then let the camera fly by the time this frame took ----
        for (const InputEvent& event : m_window.GetInputEvents())
        {
            m_input.Dispatch(event);
        }
        if (m_cameraController.TakeFocusRequest())
        {
            FocusActiveCamera();
        }
        m_window.SetCursorCaptured(m_cameraController.IsTurning());

        const Clock::time_point now = Clock::now();
        const float frameSeconds    = std::min(std::chrono::duration<float>(now - lastFrame).count(), MaxFrameSeconds);
        lastFrame                   = now;
        m_cameraController.Update(GetActiveCamera(), frameSeconds);

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

        // Whichever camera is active now, it sees the image's shape; an unchanged ratio keeps its matrices.
        Camera& camera = GetActiveCamera();
        (void)camera.SetAspectRatio(aspectRatio); // the image size is never 0, so this cannot fail
        CubePushConstants pushConstants;
        pushConstants.ViewProjection = camera.GetViewProjection();

        const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
        vkCmdPushConstants(commandBuffer, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                           &pushConstants);
        vkCmdDraw(commandBuffer, CubeVertexCount, 1, 0, 0);

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

std::size_t Wotah::App::AddCamera(const Camera& camera)
{
    m_cameras.push_back(camera);
    m_focusViews.push_back(camera);
    return m_cameras.size() - 1;
}

bool Wotah::App::SelectCamera(std::size_t index)
{
    if (index >= m_cameras.size())
    {
        Logger::Error("There is no camera {}; there are {}", index, m_cameras.size());
        return false;
    }
    m_activeCamera = index;
    return true;
}

void Wotah::App::FocusActiveCamera()
{
    // The aspect ratio comes back too; Run sets the image's ratio again before the next frame.
    m_cameras[m_activeCamera] = m_focusViews[m_activeCamera];
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
