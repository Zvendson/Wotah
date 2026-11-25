/**
 * @file app.cpp
 * The application: a Vulkan 1.3 example scene, a slowly turning orange cube seen from above one corner, and a
 * blue ball behind it, both on a grass ground under a clear blue sky, lit by a warm sun in the east and a cool
 * moon, each casting its own shadows. Uses dynamic rendering (no VkRenderPass / VkFramebuffer) with a depth image; the
 * shapes are Meshes on the GPU, camera and object matrices come in as push constants, and the lights through a uniform
 * buffer with every light's shadow map beside it.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "core/utils.hpp"
#include "input/input_event.hpp"
#include "render/height_field.hpp"
#include "render/lighting.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/sky.hpp"
#include "render/swapchain.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>

/** The clock frame times are measured with: steady, so changing the system time cannot make a frame negative. */
using Clock = std::chrono::steady_clock;

/**
 * The longest frame time the camera moves by: after a stall (the window minimized, a breakpoint) it continues from
 * where it was instead of jumping far.
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

/** The ground's fragment shader (shaders/terrain.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t TerrainFragmentShader[] = {
#include "terrain.frag.inc"
};

/** The ball's fragment shader (shaders/ball.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
};

/** The vertex shader that draws shadow casters into a light's shadow map (shaders/shadow.vert). */
static constexpr uint32_t ShadowVertexShader[] = {
#include "shadow.vert.inc"
};

/** The window's size in pixels. */
static constexpr Wotah::WindowSize MainWindowSize{600, 600};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Cube";

/** What a frame starts with; the sky covers all of it right away, so this only shows if the sky is missing. */
static constexpr VkClearColorValue BackgroundColor{{0.1f, 0.1f, 0.1f, 1.0f}};

/**
 * Where the first camera stands: south-east of the cube and a bit above it, so the top, east and south faces show, and
 * looking almost level, so the sky shows above the horizon.
 */
static constexpr glm::vec3 CornerCameraPosition{2.2f, 1.5f, 3.0f};

/** What the first camera looks at: just above the cube's center, between cube and ball. */
static constexpr glm::vec3 CornerCameraTarget{-0.2f, 0.75f, 0.0f};

/** The nearest and farthest distance the first camera sees. */
static constexpr float CornerCameraNear = 0.1f;
static constexpr float CornerCameraFar  = 100.0f; // sees the whole ground

/**
 * The ground: 64 x 64 units around the origin, a height point every half unit, so hills can be shaped finely. Flat
 * at height 0 for now.
 */
static constexpr float    GroundSize  = 64.0f;
static constexpr uint32_t GroundCells = 128;

/**
 * Where shadow casters can stand: a sphere around the ground's middle that holds the whole ground (half its diagonal)
 * and room above it for what stands on it.
 */
static constexpr float     ShadowRoomAbove  = 2.0f;
static constexpr float     ShadowAreaRadius = std::numbers::sqrt2_v<float> * GroundSize * 0.5f + ShadowRoomAbove;
static constexpr glm::vec3 ShadowAreaCenter{0.0f};

/**
 * How far the shadow map's depths are pushed away from the light, so a lit surface does not shadow itself: 2 of the
 * map's smallest depth steps, and twice the triangle's slope. Found by looking at the pictures; with
 * shaders/lighting.glsl lifting the looked-up point, this leaves the open ground clean without the shadows letting go
 * of the shapes.
 */
static constexpr float ShadowDepthBiasConstant = 2.0f;
static constexpr float ShadowDepthBiasSlope    = 2.0f;

/** Where the cube stands on the ground (x, z), and half its size of 1: how far its center is above the ground. */
static constexpr glm::vec2 CubeSpot{0.0f, 0.0f};
static constexpr float     CubeHalfSize = 0.5f;

/**
 * How fast the cube turns around its upright axis: once every 10 seconds, slow enough to watch every face go by.
 * Positive turns counterclockwise, seen from above.
 */
static constexpr float CubeTurnSeconds = 10.0f;
static constexpr float CubeTurnSpeed   = 2.0f * std::numbers::pi_v<float> / CubeTurnSeconds; // radians per second

/** The axis the cube turns around: straight up through its center, so it keeps standing on the ground. */
static constexpr glm::vec3 CubeTurnAxis{0.0f, 1.0f, 0.0f};

/** The ball's size and where it lies on the ground (x, z): behind the cube and to its left, as seen at the start. */
static constexpr float     BallRadius = 0.5f;
static constexpr glm::vec2 BallSpot{-1.3f, -0.9f};

/** How finely the ball is cut: enough for a smooth outline at this size. */
static constexpr uint32_t BallSegments = 48;
static constexpr uint32_t BallRings    = 24;

/** The ambient light: the blue sky lighting every surface a little, also those facing away from sun and moon. */
static constexpr glm::vec3 SkyAmbient{0.2f, 0.24f, 0.32f};

/** The sun: warm white, in the east at 45 degrees above the horizon (compass: render/lighting.hpp). */
static constexpr float     SunAzimuth   = glm::radians(90.0f);
static constexpr float     SunElevation = glm::radians(45.0f);
static constexpr glm::vec3 SunColor{1.0f, 0.95f, 0.85f};
static constexpr float     SunIntensity = 0.9f;

/** Where the sun sits in SceneLights::DirectionalLights; the sky draws its disk from that light. */
static constexpr std::size_t SunLightIndex = 0;

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

/** The depth-only pipeline that draws every shadow caster into a light's shadow map: mesh vertices, no culling. */
static Wotah::PipelineSettings MakeShadowSettings(VkFormat shadowFormat)
{
    // Both sides of every triangle cast, so open or thin shapes cast too; the depth bias keeps lit faces clean.
    Wotah::PipelineSettings settings;
    settings.VertexShader      = ShadowVertexShader;
    settings.DepthFormat       = shadowFormat;
    settings.VertexBindings    = Wotah::MeshVertexBindings;
    settings.VertexAttributes  = Wotah::MeshVertexAttributes;
    settings.CullMode          = VK_CULL_MODE_NONE;
    settings.DepthTest         = true;
    settings.DepthWrite        = true;
    settings.DepthBiasConstant = ShadowDepthBiasConstant;
    settings.DepthBiasSlope    = ShadowDepthBiasSlope;
    settings.PushConstants     = MeshPushConstantRanges;
    return settings;
}

/** One thing standing in the scene this frame: how it is drawn, what it is, and where. */
struct SceneObject
{
    /** Not owned: the pipeline that draws it in the scene. */
    const Wotah::Pipeline* Pipeline = nullptr;
    /** Not owned: its shape on the GPU. */
    const Wotah::Mesh* Mesh = nullptr;
    /** Its own space to world space. */
    glm::mat4 Model{1.0f};
};

/**
 * Records every shadow map of every light that casts shadows, each cascade: everything in objects drawn as that light
 * sees that cascade. Before the scene, outside its drawing.
 */
static void RecordShadows(VkCommandBuffer commandBuffer, const Wotah::Lighting& lighting,
                          const Wotah::Pipeline& shadowPipeline, std::span<const SceneObject> objects)
{
    const Wotah::ShadowMaps& maps = lighting.GetShadowMaps();
    for (uint32_t layer = 0; layer < Wotah::MaxDirectionalLights * Wotah::ShadowCascadeCount; ++layer)
    {
        const uint32_t                 index     = layer / Wotah::ShadowCascadeCount;
        const uint32_t                 cascade   = layer % Wotah::ShadowCascadeCount;
        const std::optional<glm::mat4> lightView = lighting.FindShadowViewProjection(index, cascade);
        if (!lightView || !maps.BeginLayer(commandBuffer, layer))
        {
            continue; // casts no shadow (or no such map, logged)
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline.GetHandle());
        MeshPushConstants pushConstants;
        pushConstants.ViewProjection = *lightView;
        for (const SceneObject& object : objects)
        {
            pushConstants.Model = object.Model;
            vkCmdPushConstants(commandBuffer, shadowPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(pushConstants), &pushConstants);
            object.Mesh->Draw(commandBuffer);
        }
        maps.EndLayer(commandBuffer, layer);
    }
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

    /** The sky with the sun, drawn first in every frame. */
    Wotah::Sky Sky;

    /** The pipelines: the grass ground, the orange cube with black edges, and the blue ball. */
    Wotah::Pipeline TerrainPipeline;
    Wotah::Pipeline CubePipeline;
    Wotah::Pipeline BallPipeline;

    /** Draws every shadow caster into the lights' shadow maps. */
    Wotah::Pipeline ShadowPipeline;

    /** The ground's shape, and the ground and shapes on the GPU. */
    Wotah::HeightField GroundShape;
    Wotah::Mesh        Terrain;
    Wotah::Mesh        Cube;
    Wotah::Mesh        Ball;

    /** Where cube and ball stand: on the ground, worked out in Create. */
    glm::vec3 CubeCenter{0.0f};
    glm::mat4 BallModel{1.0f};

    /** How far the cube has turned around its upright axis, in radians; every frame turns it on. */
    float CubeAngle = 0.0f;
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
                                   MakeMeshSettings(BallFragmentShader, colorFormat, depthFormat, setLayouts)) ||
        !state.TerrainPipeline.Create(m_device,
                                      MakeMeshSettings(TerrainFragmentShader, colorFormat, depthFormat, setLayouts)) ||
        !state.Sky.Create(m_device, colorFormat, depthFormat) ||
        !state.ShadowPipeline.Create(m_device, MakeShadowSettings(state.Lighting.GetShadowMaps().GetFormat())))
    {
        return false;
    }

    // A sun and a moon, both shining at once for now; a day and night cycle will move them and change their light.
    DirectionalLight sun;
    sun.Direction = MakeLightDirection(SunAzimuth, SunElevation);
    sun.Color     = SunColor;
    sun.Intensity = SunIntensity;
    DirectionalLight moon;
    moon.Direction             = MoonDirection;
    moon.Color                 = MoonColor;
    moon.Intensity             = MoonIntensity;
    m_lights.AmbientColor      = SkyAmbient;
    m_lights.DirectionalLights = {sun, moon};                          // the sun first: SunLightIndex
    m_lights.Shadows           = {ShadowAreaCenter, ShadowAreaRadius}; // every light casts shadows on the ground

    // The ground and the shapes go to GPU-only memory once; Create waits until they are there.
    state.GroundShape = MakeFlatHeightField(GroundSize, GroundSize, GroundCells, GroundCells);
    if (!state.Terrain.Create(m_device, MakeTerrainMesh(state.GroundShape)) ||
        !state.Cube.Create(m_device, MakeCube()) ||
        !state.Ball.Create(m_device, MakeSphere(BallRadius, BallSegments, BallRings)))
    {
        return false;
    }

    // Cube and ball stand on the ground wherever it is, so they keep standing on it once there are hills.
    const std::optional<float> cubeGround = FindHeight(state.GroundShape, CubeSpot.x, CubeSpot.y);
    const std::optional<float> ballGround = FindHeight(state.GroundShape, BallSpot.x, BallSpot.y);
    if (!cubeGround || !ballGround)
    {
        Logger::Error("Cube and ball must stand on the ground's area");
        return false;
    }
    state.CubeCenter = {CubeSpot.x, *cubeGround + CubeHalfSize, CubeSpot.y};
    state.BallModel  = glm::translate(glm::mat4(1.0f), {BallSpot.x, *ballGround + BallRadius, BallSpot.y});

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
    Start();

    // Drawing gets its own thread, so the window's events never hold it up: while the user moves or resizes the window,
    // Windows keeps the main thread inside WaitEvents until the mouse is let go.
    m_hasDrawingFailed = false;
    m_isDrawing        = true;
    std::jthread drawing([this](const std::stop_token& stop) { DrawFrames(stop); });

    // The main thread only handles the window's events, until the window is closed or drawing ended on its own.
    while (!m_window.ShouldClose() && m_isDrawing)
    {
        m_window.WaitEvents();
    }

    // Events are still handled while the drawing thread finishes its frame. Guess: a driver may need the window's
    // events to finish showing an image, and would wait forever if the main thread only waited for the thread.
    drawing.request_stop();
    while (m_isDrawing)
    {
        m_window.WaitEvents(); // the drawing thread wakes it when it ends
    }
    drawing.join();

    if (m_hasDrawingFailed)
    {
        Logger::Critical("Stopped drawing after a Vulkan error");
    }
    Stop();
    return !m_hasDrawingFailed;
}

void Wotah::App::DrawFrames(const std::stop_token& stop)
{
    Clock::time_point lastFrame = Clock::now();
    while (!stop.stop_requested())
    {
        // Nothing to draw into while the window is minimized or has no area: sleep until it has, or until told to stop.
        if (!m_window.WaitForDrawableArea(stop))
        {
            break;
        }

        const Clock::time_point now = Clock::now();
        const float frameSeconds    = std::min(std::chrono::duration<float>(now - lastFrame).count(), MaxFrameSeconds);
        lastFrame                   = now;
        if (!DrawFrame(frameSeconds))
        {
            m_hasDrawingFailed = true; // Run reads it after join, which makes the write visible
            break;
        }
    }

    // The main thread may be asleep in WaitEvents; it needs to know drawing is over.
    m_isDrawing = false;
    m_window.Wake();
}

bool Wotah::App::DrawFrame(float frameSeconds)
{
    Renderer&    renderer = m_state->Renderer;
    const State& scene    = *m_state;

    // ---- Input: hand it out, then let the camera fly by the time this frame took ----
    m_window.TakeInputEvents(m_frameInput);
    for (const InputEvent& event : m_frameInput)
    {
        m_input.Dispatch(event);
    }
    if (m_cameraController.TakeFocusRequest())
    {
        FocusActiveCamera();
    }
    m_window.RequestCursorCaptured(m_cameraController.IsTurning());
    m_cameraController.Update(GetActiveCamera(), frameSeconds);

    // The cube turns on by the time this frame took, so it turns at the same speed whatever the frame rate.
    m_state->CubeAngle = TurnAngle(m_state->CubeAngle, CubeTurnSpeed, frameSeconds);

    // A new window size, or a swapchain the system no longer finds fitting: make it again before drawing. While the
    // user drags the window's edge this happens every few frames, so the picture follows the edge.
    if (m_window.TakeResize() || m_state->Swapchain.IsOutOfDate())
    {
        if (!renderer.RecreateSwapchain(m_device, m_window))
        {
            return false;
        }
    }

    const FrameStart start = renderer.BeginFrame();
    if (start != FrameStart::Ready)
    {
        return start == FrameStart::Skip; // a skipped frame is no failure
    }

    // Whichever camera is active now, it sees the image's shape, also right after a resize; an unchanged ratio keeps
    // its matrices. The image is never 0 wide or high (drawing waits while the window has no area), so the ratio is
    // always valid.
    const VkExtent2D extent = m_state->Swapchain.GetExtent();
    Camera&          camera = GetActiveCamera();
    (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    // The GPU is done with the last frame (BeginFrame waited for it), so the lights can be rewritten. Bad lights are
    // logged by Update and the last good ones stay, so the frame still draws.
    (void)m_state->Lighting.Update(m_lights, camera);

    // What stands in the scene this frame: the ground where its heights say, the turning cube, and the ball.
    const std::array<SceneObject, 3> objects{{
        {&scene.TerrainPipeline, &scene.Terrain, glm::mat4(1.0f)},
        {&scene.CubePipeline, &scene.Cube,
         glm::rotate(glm::translate(glm::mat4(1.0f), scene.CubeCenter), scene.CubeAngle, CubeTurnAxis)},
        {&scene.BallPipeline, &scene.Ball, scene.BallModel},
    }};

    // Every light's shadow map first, as the scene's shaders read them all; then the scene itself.
    const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
    RecordShadows(commandBuffer, scene.Lighting, scene.ShadowPipeline, objects);
    renderer.BeginScene();

    // The sky first: it tests no depth, so everything after it is drawn in front. Without a sun light it is not drawn.
    MeshPushConstants pushConstants;
    pushConstants.ViewProjection = camera.GetViewProjection();
    if (SunLightIndex < m_lights.DirectionalLights.size())
    {
        scene.Sky.Draw(commandBuffer, pushConstants.ViewProjection, m_lights.DirectionalLights[SunLightIndex]);
    }

    // The depth test sorts out which is in front, so the order of ground and shapes does not matter.
    for (const SceneObject& object : objects)
    {
        pushConstants.Model = object.Model;
        DrawMesh(commandBuffer, *object.Pipeline, scene.Lighting, *object.Mesh, pushConstants);
    }
    return renderer.EndFrame();
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
