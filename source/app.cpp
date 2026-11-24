/**
 * @file app.cpp
 * The application: a Vulkan 1.3 example scene, an orange cube seen from above one corner so three faces show, and a
 * blue ball behind it, lit by a warm sun and a cool moon. Uses dynamic rendering (no VkRenderPass / VkFramebuffer) with
 * a depth image; the shapes are Meshes on the GPU, camera and object matrices come in as push constants, and the lights
 * through a uniform buffer.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "input/input_event.hpp"
#include "render/lighting.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/swapchain.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

/** The clock frame times are measured with: steady, so changing the system time cannot make a frame negative. */
using Clock = std::chrono::steady_clock;

/**
 * The longest frame time the camera moves by: after a stall (dragging the window, a breakpoint) it continues from where
 * it was instead of jumping far.
 */
static constexpr float MaxFrameSeconds = 0.1f;

/** The vertex shader of every mesh (shaders/mesh.vert), compiled to SPIR-V by the build. */
static constexpr uint32_t MeshVertexShader[] = {
#include "mesh.vert.inc"
};

/** The cube's fragment shader (shaders/cube.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t CubeFragmentShader[] = {
#include "cube.frag.inc"
};

/** The ball's fragment shader (shaders/ball.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
};

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

/** The ball's size and where it lies: behind the cube and to its left, as seen from the start view. */
static constexpr float     BallRadius = 0.5f;
static constexpr glm::vec3 BallPosition{-1.3f, 0.0f, -0.9f};

/** How finely the ball is cut: enough for a smooth outline at this size. */
static constexpr uint32_t BallSegments = 48;
static constexpr uint32_t BallRings    = 24;

/** The ambient light: a dim, slightly blue night sky that keeps surfaces facing away from sun and moon visible. */
static constexpr glm::vec3 SkyAmbient{0.12f, 0.12f, 0.15f};

/** The sun: warm white, high above, from the right and a bit from the front, as the light was before. */
static constexpr glm::vec3 SunDirection{-0.4f, -1.0f, -0.25f};
static constexpr glm::vec3 SunColor{1.0f, 0.95f, 0.85f};
static constexpr float     SunIntensity = 0.9f;

/** The moon: cool blue and weak, low from the front left, so its light shows on the faces the start view sees. */
static constexpr glm::vec3 MoonDirection{0.3f, -0.3f, -1.0f};
static constexpr glm::vec3 MoonColor{0.45f, 0.55f, 1.0f};
static constexpr float     MoonIntensity = 0.35f;

/** What mesh.vert gets per draw; matches its PushConstants block. */
struct MeshPushConstants
{
    /** World space to clip space: the active camera. */
    glm::mat4 ViewProjection{1.0f};
    /** The mesh's own space to world space: where the object is. */
    glm::mat4 Model{1.0f};
};

// Every GPU offers at least 128 bytes of push constants (Vulkan spec, "Required Limits", maxPushConstantsSize).
static_assert(sizeof(MeshPushConstants) <= 128, "the push constants must fit the 128 bytes every GPU offers");

/** The one push constant range: camera and object matrix, read by the vertex shader. */
static constexpr std::array<VkPushConstantRange, 1> MeshPushConstantRanges{
    {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants)}}};

/**
 * The settings every mesh pipeline shares: mesh.vert, mesh vertices, back faces culled, depth tested and written so
 * nearer surfaces hide farther ones whatever order they are drawn in, and the scene's lights at set 0.
 */
static Wotah::PipelineSettings MakeMeshSettings(std::span<const uint32_t> fragmentShader, VkFormat colorFormat,
                                                VkFormat depthFormat, std::span<const VkDescriptorSetLayout> setLayouts)
{
    Wotah::PipelineSettings settings;
    settings.VertexShader         = MeshVertexShader;
    settings.FragmentShader       = fragmentShader;
    settings.ColorFormat          = colorFormat;
    settings.DepthFormat          = depthFormat;
    settings.VertexBindings       = Wotah::MeshVertexBindings;
    settings.VertexAttributes     = Wotah::MeshVertexAttributes;
    settings.CullMode             = VK_CULL_MODE_BACK_BIT;
    settings.FrontFace            = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    settings.DepthTest            = true;
    settings.DepthWrite           = true;
    settings.PushConstants        = MeshPushConstantRanges;
    settings.DescriptorSetLayouts = setLayouts;
    return settings;
}

/** Records one mesh's draw: its pipeline, the lights, the camera and its place, then the mesh. */
static void DrawMesh(VkCommandBuffer commandBuffer, const Wotah::Pipeline& pipeline, const Wotah::Lighting& lighting,
                     const Wotah::Mesh& mesh, const MeshPushConstants& pushConstants)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
    lighting.Bind(commandBuffer, pipeline.GetLayout());
    vkCmdPushConstants(commandBuffer, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
                       &pushConstants);
    mesh.Draw(commandBuffer);
}

/**
 * The Vulkan objects the application creates on top of the device. Declared in creation order, so they are destroyed
 * in reverse: the meshes and pipelines first, the renderer before the swapchain it draws into.
 */
struct Wotah::App::State
{
    /** The swapchain, its images and one view per image. */
    Wotah::Swapchain Swapchain;

    /** The command buffer, depth image and synchronization of each frame. */
    Wotah::Renderer Renderer;

    /** The scene's lights on the GPU, rewritten every frame. */
    Wotah::Lighting Lighting;

    /** The pipelines: the orange cube with black edges, and the blue ball. */
    Wotah::Pipeline CubePipeline;
    Wotah::Pipeline BallPipeline;

    /** The scene's shapes, on the GPU. */
    Wotah::Mesh Cube;
    Wotah::Mesh Ball;
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

    // The renderer comes before the pipelines: they need its depth format.
    if (!m_window.Create(MainWindowSize, AppName) || !m_device.Create(m_window, AppName) ||
        !state.Swapchain.Create(m_device, m_window) || !state.Renderer.Create(m_device, state.Swapchain))
    {
        return false;
    }
    state.Renderer.SetClearColor(BackgroundColor);

    // The lights come before the pipelines too: they read them through its descriptor set layout.
    if (!state.Lighting.Create(m_device))
    {
        return false;
    }
    const VkFormat                             colorFormat = state.Swapchain.GetFormat();
    const VkFormat                             depthFormat = state.Renderer.GetDepthFormat();
    const std::array<VkDescriptorSetLayout, 1> setLayouts{state.Lighting.GetSetLayout()};
    if (!state.CubePipeline.Create(m_device,
                                   MakeMeshSettings(CubeFragmentShader, colorFormat, depthFormat, setLayouts)) ||
        !state.BallPipeline.Create(m_device,
                                   MakeMeshSettings(BallFragmentShader, colorFormat, depthFormat, setLayouts)))
    {
        return false;
    }

    // A sun and a moon, both shining at once for now; a day and night cycle will move them and change their light.
    DirectionalLight sun;
    sun.Direction = SunDirection;
    sun.Color     = SunColor;
    sun.Intensity = SunIntensity;
    DirectionalLight moon;
    moon.Direction             = MoonDirection;
    moon.Color                 = MoonColor;
    moon.Intensity             = MoonIntensity;
    m_lights.AmbientColor      = SkyAmbient;
    m_lights.DirectionalLights = {sun, moon};

    // The shapes go to GPU-only memory once; Create waits until they are there.
    if (!state.Cube.Create(m_device, MakeCube()) ||
        !state.Ball.Create(m_device, MakeSphere(BallRadius, BallSegments, BallRings)))
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

    // The camera controller gets whatever input no handler before it used; a UI will be added in front of it.
    m_input.AddHandler(m_cameraController);
    return true;
}

bool Wotah::App::Run()
{
    Renderer&    renderer = m_state->Renderer;
    const State& scene    = *m_state;

    // The objects stay where they are; only the camera moves.
    const glm::mat4 cubeModel{1.0f}; // at the origin
    const glm::mat4 ballModel = glm::translate(glm::mat4(1.0f), BallPosition);

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
        // The GPU is done with the last frame (BeginFrame waited for it), so the lights can be rewritten. Bad lights
        // are logged by Update and the last good ones stay, so the frame still draws.
        (void)m_state->Lighting.Update(m_lights);

        MeshPushConstants pushConstants;
        pushConstants.ViewProjection = camera.GetViewProjection();

        // The depth test sorts out which is in front, so the draw order does not matter.
        const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
        pushConstants.Model                 = cubeModel;
        DrawMesh(commandBuffer, scene.CubePipeline, scene.Lighting, scene.Cube, pushConstants);
        pushConstants.Model = ballModel;
        DrawMesh(commandBuffer, scene.BallPipeline, scene.Lighting, scene.Ball, pushConstants);

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
