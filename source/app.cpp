/**
 * @file app.cpp
 * The application: a Vulkan 1.3 example scene, a sandy beach along a wandering coast with dunes and green hills
 * behind it and the sea in front, both reaching the horizon in every direction, and a blue ball dropped from the air
 * into the shallow water, under a clear sky the sea mirrors. The day runs: the sun rises in the east over the land,
 * crosses the south and sets in the west over the sea, lighting the scene and casting the shadows, and at night the
 * full moon gives a faint blue light under the stars. Gravity and collisions come from the physics world (Jolt); the
 * water has no physics yet, so the ball falls through it onto the sand. Uses dynamic rendering (no VkRenderPass /
 * VkFramebuffer) with a depth image; the ball is a Mesh on the GPU, land and sea are geometry clipmaps shaped by their
 * shaders, camera and object matrices come in as push constants, and the lights through a uniform buffer with every
 * light's shadow map beside it. Over the scene lie a small stats window, a list of the controls and a control bar (Dear
 * ImGui) that pauses the world and resets the scene or the camera; R resets the scene too. Tab opens a settings window
 * with sliders for the time of day and the day's run, sun, moon and sky, shadows, gravity, time, the ball's bounce and
 * the field of view.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "input/input_event.hpp"
#include "physics/physics_world.hpp"
#include "render/lighting.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/pipeline.hpp"
#include "render/renderer.hpp"
#include "render/sky.hpp"
#include "render/sky_clock.hpp"
#include "render/sky_lights.hpp"
#include "render/swapchain.hpp"
#include "render/terrain_renderer.hpp"
#include "render/ui_renderer.hpp"
#include "render/water.hpp"
#include "render/water_renderer.hpp"
#include "terrain/terrain_shape.hpp"
#include "ui/control_bar.hpp"
#include "ui/hints_window.hpp"
#include "ui/settings_window.hpp"
#include "ui/stats_window.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

/** The clock frame times are measured with: steady, so changing the system time cannot make a frame negative. */
using Clock = std::chrono::steady_clock;

/**
 * The longest frame time the camera and the physics move by: after a stall (the window minimized, a breakpoint) they
 * continue from where they were instead of jumping far.
 */
static constexpr float MaxFrameSeconds = 0.1f;

/** The vertex shader of every mesh (shaders/mesh.vert), compiled to SPIR-V by the build. */
static constexpr uint32_t MeshVertexShader[] = {
#include "mesh.vert.inc"
};

/** The ball's fragment shader (shaders/ball.frag), compiled to SPIR-V by the build. */
static constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
};

/** The vertex shader that draws shadow casters into a light's shadow map (shaders/shadow.vert). */
static constexpr uint32_t ShadowVertexShader[] = {
#include "shadow.vert.inc"
};

/**
 * The window's size at the start, in pixels: 1280 x 720 (HD, 16:9). It fits on nearly every screen, small 1366 x 768
 * laptops too, and leaves the scene room beside the UI's cards.
 */
static constexpr Wotah::WindowSize MainWindowSize{1280, 720};

/** The window's title and the application name Vulkan is told. */
static constexpr const char* AppName = "Vulkan Cube";

/** What a frame starts with; the sky covers all of it right away, so this only shows if the sky is missing. */
static constexpr VkClearColorValue BackgroundColor{{0.1f, 0.1f, 0.1f, 1.0f}};

/**
 * The land: the beach the default TerrainShape makes, its waterline through the origin, the sea to the west (-x),
 * where the sun sets.
 * The sea's surface lies at the beach's sea level.
 */
static constexpr Wotah::TerrainShape LandShape{};
static constexpr Wotah::Sea          SeaSurface{LandShape.SeaLevel};

/**
 * Where the first camera stands: over the dry beach, 4 units above the sand (as from a lifeguard's tower), looking
 * west-south-west across the waterline into the shallow water where the ball falls in, with the coast running across
 * the picture and the evening sun setting into the sea left of the middle.
 */
static constexpr glm::vec2 StartCameraSpot{12.0f, 14.0f};
static constexpr float     StartCameraHeight = 4.0f;
static constexpr glm::vec3 StartCameraTarget{-17.0f, 0.0f, 6.0f};

/**
 * The nearest and farthest distance the first camera sees: far enough for the land and sea to reach the haze, which
 * hides everything past a few hundred units.
 */
static constexpr float StartCameraNear = 0.1f;
static constexpr float StartCameraFar  = 2000.0f;

/**
 * The land the physics knows: a square of heights around the origin, 255 units across, one a unit (Jolt works best
 * with a power of two to a side). shortcut: it covers where the ball can get to; once things travel far, the physics
 * would take squares around each moving body as it goes.
 */
static constexpr uint32_t PhysicsLandSamples = 256;
static constexpr float    PhysicsLandSpacing = 1.0f;

/**
 * Where shadow casters can stand: a sphere around the camera, wide enough for hills well beyond the shadows' reach
 * (SceneLights::ShadowReach) to still throw their shadows into it when the sun is low.
 */
static constexpr float ShadowCasterRadius = 150.0f;

/**
 * How far the shadow map's depths are pushed away from the light, so a lit surface does not shadow itself: 2 of the
 * map's smallest depth steps, and twice the triangle's slope. Found by looking at the pictures; with
 * shaders/lighting.glsl lifting the looked-up point, this leaves the open ground clean without the shadows letting go
 * of the shapes.
 */
static constexpr float ShadowDepthBiasConstant = 2.0f;
static constexpr float ShadowDepthBiasSlope    = 2.0f;

/**
 * The ball: half a unit across, dropped from 4 units above the sea (its lowest point) 16 units out from the waterline,
 * where the water is about a unit deep, in front of the start camera. The water has no physics yet: it falls through
 * onto the sand.
 */
static constexpr float     BallRadius = 0.5f;
static constexpr glm::vec2 BallSpot{-16.0f, 0.0f};
static constexpr float     BallDropHeight = 4.0f;

/**
 * The surfaces, chosen by eye: sand grips and hardly bounces, the ball is like rubber. Two bodies in contact bounce
 * like the bouncier one, so the ball bounces on the sand.
 */
static constexpr Wotah::BodySurface SandSurface{0.8f, 0.1f};
static constexpr Wotah::BodySurface RubberSurface{0.7f, 0.6f};

/**
 * How fast the ball rolling on the sand slows down by itself, per second: the sand's rolling resistance, which the
 * physics has no other way to know. Chosen by testing: a ball knocked aside rolls a metre or two.
 */
static constexpr float BallSpinDamping = 1.0f;
static constexpr float BallMoveDamping = 0.2f;

/** How finely the ball is cut: enough for a smooth outline at this size. */
static constexpr uint32_t BallSegments = 48;
static constexpr uint32_t BallRings    = 24;

/** How bright the sun is as the scene starts (the Sun brightness setting); chosen by eye. */
static constexpr float SunIntensity = 0.9f;

/**
 * The day as the scene starts: 9 in the morning, the sun in the east-south-east about 38 degrees up, behind the start
 * view, so the shapes' shadows fall away from the camera; a day that reaches 60 degrees at noon (about 30 degrees
 * north, as in Cairo at an equinox) and lasts 10 minutes, so a sunset shows within a few minutes.
 */
static constexpr float StartHours        = 9.0f;
static constexpr float NoonHeightDegrees = 60.0f;
static constexpr float DayLengthMinutes  = 10.0f;

/** Where sun and moon sit in SceneLights::DirectionalLights; the sky draws the sun's disk from its light. */
static constexpr std::size_t SunLightIndex  = 0;
static constexpr std::size_t MoonLightIndex = 1;

/**
 * Where the settings start beyond the values above: time runs as fast as the clock, the day runs, sky and moon give all
 * their light.
 */
static constexpr float RealTimeSpeed = 1.0f;
static constexpr float FullSkyLight  = 1.0f;
static constexpr float FullMoonLight = 1.0f;

/** The settings as the scene starts: the values above, in the settings window's units. */
static Wotah::SceneSettings MakeDefaultSettings()
{
    Wotah::SceneSettings settings;
    settings.TimeOfDayHours       = StartHours;
    settings.IsDayRunning         = true;
    settings.DayLengthMinutes     = DayLengthMinutes;
    settings.SunNoonHeightDegrees = NoonHeightDegrees;
    settings.SunBrightness        = SunIntensity;
    settings.MoonBrightness       = FullMoonLight;
    settings.SkyLight             = FullSkyLight;
    settings.HasShadows           = true;
    settings.Gravity              = Wotah::StandardGravity;
    settings.TimeSpeed            = RealTimeSpeed;
    settings.BallBounciness       = RubberSurface.Bounciness;
    settings.FieldOfViewDegrees   = glm::degrees(Wotah::Camera{}.GetFieldOfView()); // every camera's default
    return settings;
}

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
 * Records every shadow map of every light that casts shadows, each cascade: everything in objects, and the land within
 * landReach of the camera, drawn as that light sees that cascade. Before the scene, outside its drawing.
 */
static void RecordShadows(VkCommandBuffer commandBuffer, const Wotah::Lighting& lighting,
                          const Wotah::Pipeline& shadowPipeline, std::span<const SceneObject> objects,
                          const Wotah::TerrainRenderer& land, float landReach)
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
        land.DrawShadow(commandBuffer, *lightView, landReach);
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
 * in reverse: the UI's renderer, the meshes and pipelines first, the renderer before the swapchain it draws into.
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

    /** The land and the sea, to the horizon around the camera. */
    Wotah::TerrainRenderer Land;
    Wotah::WaterRenderer   Sea;

    /** The blue ball's pipeline and shape. */
    Wotah::Pipeline BallPipeline;
    Wotah::Mesh     Ball;

    /** Draws every shape that casts a shadow into the lights' shadow maps (the land draws itself). */
    Wotah::Pipeline ShadowPipeline;

    /** Gravity and collisions: the land and the ball. */
    Wotah::PhysicsWorld Physics;
    Wotah::BodyId       BallBody;

    /** Where the ball started, so Reset scene can put it back. */
    Wotah::BodySettings BallStart;

    /** Draws the UI over the scene, last in every frame. */
    Wotah::UiRenderer UiRenderer;
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
    if (!state.BallPipeline.Create(m_device,
                                   MakeMeshSettings(BallFragmentShader, colorFormat, depthFormat, setLayouts)) ||
        !state.Sky.Create(m_device, colorFormat, depthFormat) ||
        !state.ShadowPipeline.Create(m_device, MakeShadowSettings(state.Lighting.GetShadowMaps().GetFormat())) ||
        !state.Land.Create(m_device, colorFormat, depthFormat, state.Lighting) || !state.Land.SetShape(LandShape) ||
        !state.Sea.Create(m_device, colorFormat, depthFormat, state.Lighting, state.Renderer.GetSceneDepth()))
    {
        return false;
    }

    // A sun and a moon; the settings put them where the time of day has them and give them their light
    // (ApplySettings, below).
    m_lights.DirectionalLights = {DirectionalLight{}, DirectionalLight{}}; // SunLightIndex, then MoonLightIndex

    // The ball goes to GPU-only memory once; Create waits until it is there.
    if (!state.Ball.Create(m_device, MakeSphere(BallRadius, BallSegments, BallRings)) || !CreatePhysics())
    {
        return false;
    }

    // The UI over the scene: ImGui first, then its renderer, which attaches to it and draws into the same frames.
    if (!m_ui.Create() || !state.UiRenderer.Create(m_device, colorFormat, depthFormat))
    {
        return false;
    }

    // The first camera stands over the beach, at its height above the sand wherever that is, looking out to where the
    // ball falls in; Run keeps every active camera's aspect ratio in step with the image.
    Camera&     start  = m_cameras.front();
    const float ground = GetTerrainHeight(LandShape, StartCameraSpot);
    start.SetPosition({StartCameraSpot.x, ground + StartCameraHeight, StartCameraSpot.y});
    start.SetTarget(StartCameraTarget);
    if (!start.SetClipDistances(StartCameraNear, StartCameraFar))
    {
        return false;
    }
    m_focusViews.front() = start;

    // The settings start as the scene was just set up; putting them in once makes them the one source from now on.
    m_settings = MakeDefaultSettings();
    ApplySettings();

    // The UI is asked first and keeps what lands on its windows; the camera controller and the scene's keys get the
    // rest.
    m_input.AddHandler(m_ui);
    m_input.AddHandler(m_cameraController);
    m_input.AddHandler(m_sceneShortcuts);
    return true;
}

bool Wotah::App::CreatePhysics()
{
    State& state = *m_state;
    if (!state.Physics.Create())
    {
        return false;
    }

    // The land collides where it is drawn: its heights from the same formula, around the origin.
    const TerrainPatch land = SampleTerrainPatch(LandShape, glm::vec2(0.0f), PhysicsLandSpacing, PhysicsLandSamples);
    const glm::vec3    corner{land.Corner.x, 0.0f, land.Corner.y};
    if (!state.Physics.AddHeightField(land.Heights, land.SamplesPerSide, land.Spacing, corner, SandSurface))
    {
        return false;
    }

    // The ball starts above the sea, over land the physics knows, so it lands on something.
    const std::optional<float> seaHeight = FindWaterHeight(SeaSurface, BallSpot.x, BallSpot.y);
    if (!seaHeight)
    {
        Logger::Error("The ball must start above the sea");
        return false;
    }
    BodySettings ball;
    ball.Position    = {BallSpot.x, *seaHeight + BallDropHeight + BallRadius, BallSpot.y};
    ball.Surface     = RubberSurface;
    ball.SpinDamping = BallSpinDamping;
    ball.MoveDamping = BallMoveDamping;

    const std::optional<BodyId> ballBody = state.Physics.AddSphere(BallRadius, ball);
    if (!ballBody)
    {
        return false;
    }
    state.BallBody  = *ballBody;
    state.BallStart = ball; // kept for Reset scene, which puts it back there
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
    if (m_sceneShortcuts.TakeResetRequest())
    {
        ResetScene();
    }
    if (m_sceneShortcuts.TakeSettingsRequest())
    {
        m_isSettingsOpen = !m_isSettingsOpen;
    }
    m_window.RequestCursorCaptured(m_cameraController.IsTurning());
    m_cameraController.Update(GetActiveCamera(), frameSeconds);

    // The world moves on by the time this frame took, as fast as the settings say, unless paused: the ball falls,
    // bounces and rolls, and the day runs on, moving sun and moon and changing the light.
    if (!m_isPaused)
    {
        const float worldSeconds = frameSeconds * m_settings.TimeSpeed;
        m_state->Physics.Step(worldSeconds);
        if (m_settings.IsDayRunning)
        {
            m_settings.TimeOfDayHours =
                AdvanceTimeOfDay(m_settings.TimeOfDayHours, worldSeconds, m_settings.DayLengthMinutes);
            ApplySkySettings();
        }
    }

    // The sun's rays and glow and the stars move a little in real time, paused or not, like the camera.
    m_lights.SkySeconds = std::fmod(m_lights.SkySeconds + frameSeconds, SkyLoopSeconds);

    // A new window size, or a swapchain the system no longer finds fitting: make it again before drawing. While the
    // user drags the window's edge this happens every few frames, so the picture follows the edge.
    if (m_window.TakeResize() || m_state->Swapchain.IsOutOfDate())
    {
        if (!renderer.RecreateSwapchain(m_device, m_window))
        {
            return false;
        }
    }

    // The UI's windows for this frame, at the swapchain's size; only shapes on the CPU so far, drawn after the scene.
    // What was clicked happens right away, so a reset scene is already drawn in this frame.
    ShowUi(frameSeconds);

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
    // And the settings' field of view, also after F put a camera back to its focus view; the slider's range lies well
    // inside the 0 to 180 degrees a camera takes, so it always fits.
    (void)camera.SetFieldOfView(glm::radians(m_settings.FieldOfViewDegrees));
    // Shadow casters stand around the camera, wherever it goes; land and sea follow it to the horizon.
    const glm::vec3 viewer = camera.GetPosition();
    m_lights.Shadows       = m_settings.HasShadows ? ShadowArea{viewer, ShadowCasterRadius} : ShadowArea{};
    m_state->Land.FollowViewer(viewer);
    m_state->Sea.FollowViewer(viewer);

    // The GPU is done with the last frame (BeginFrame waited for it), so the lights can be rewritten. Bad lights are
    // logged by Update and the last good ones stay, so the frame still draws.
    (void)m_state->Lighting.Update(m_lights, camera);

    // The shapes in the scene this frame: the ball where the physics has it (always found: Create added it).
    const std::array<SceneObject, 1> objects{{
        {&scene.BallPipeline, &scene.Ball, scene.Physics.FindTransform(scene.BallBody).value_or(glm::mat4(1.0f))},
    }};

    // Every light's shadow map first, as the scene's shaders read them all; then the scene itself.
    const VkCommandBuffer commandBuffer = renderer.GetCommandBuffer();
    RecordShadows(commandBuffer, scene.Lighting, scene.ShadowPipeline, objects, scene.Land, ShadowCasterRadius);
    renderer.BeginScene();

    // The sky first: it tests no depth, so everything after it is drawn in front. Sun and moon are its own disks
    // (they sink behind the ground whole), not their light on the scene.
    MeshPushConstants pushConstants;
    pushConstants.ViewProjection = camera.GetViewProjection();
    scene.Sky.Draw(commandBuffer, pushConstants.ViewProjection, m_lights.SkySun, m_lights.SkyMoon, m_lights.SkySeconds);

    // The land and the shapes; the depth test sorts out which is in front, so their order does not matter.
    scene.Land.Draw(commandBuffer, pushConstants.ViewProjection, scene.Lighting);
    for (const SceneObject& object : objects)
    {
        pushConstants.Model = object.Model;
        DrawMesh(commandBuffer, *object.Pipeline, scene.Lighting, *object.Mesh, pushConstants);
    }

    // The sea last of the scene: what lies under it shows through it, as deep as the solid scene's depth says.
    renderer.BeginSeeThrough();
    scene.Sea.Draw(commandBuffer, pushConstants.ViewProjection, scene.Lighting, renderer.GetSceneDepth(), SeaSurface);

    // The UI last, so it lies over everything.
    return m_state->UiRenderer.Draw(commandBuffer) && renderer.EndFrame();
}

void Wotah::App::ShowUi(float frameSeconds)
{
    const VkExtent2D extent = m_state->Swapchain.GetExtent();
    const WindowSize pictureSize{extent.width, extent.height};

    FrameStats stats;
    stats.PictureWidth   = extent.width;
    stats.PictureHeight  = extent.height;
    stats.CameraPosition = GetActiveCamera().GetPosition();

    m_ui.BeginFrame(m_window.GetSize(), pictureSize, frameSeconds);
    ShowStatsWindow(stats);
    ShowHintsWindow();
    bool isSettingsChanged = false;
    if (m_isSettingsOpen)
    {
        const SceneSettings defaults = MakeDefaultSettings();
        isSettingsChanged            = ShowSettingsWindow(m_settings, defaults);
    }
    const ControlBarClicks clicks = ShowControlBar(m_isPaused);
    m_ui.EndFrame();

    ApplyClicks(clicks);
    if (isSettingsChanged)
    {
        ApplySettings();
    }
}

void Wotah::App::ApplySettings()
{
    ApplySkySettings();
    ApplyPhysicsSettings();
}

void Wotah::App::ApplySkySettings()
{
    // The sky's lights at this time of day (render/sky_lights.hpp): sun and moon where the clock has them, with their
    // light, the moon's disk for the sky, the sky's light, the haze and the light the ground throws back.
    SkyLightSettings sky;
    sky.TimeOfDayHours        = m_settings.TimeOfDayHours;
    sky.NoonElevation         = glm::radians(m_settings.SunNoonHeightDegrees);
    sky.SunBrightness         = m_settings.SunBrightness;
    sky.MoonBrightness        = m_settings.MoonBrightness;
    sky.SkyLight              = m_settings.SkyLight;
    const SkyLights skyLights = MakeSkyLights(sky);

    std::vector<DirectionalLight>& lights = m_lights.DirectionalLights;
    if (SunLightIndex < lights.size())
    {
        lights[SunLightIndex] = skyLights.Sun;
    }
    if (MoonLightIndex < lights.size())
    {
        lights[MoonLightIndex] = skyLights.Moon;
    }
    m_lights.SkySun       = skyLights.SunDisk;
    m_lights.SkyMoon      = skyLights.MoonDisk;
    m_lights.AmbientColor = skyLights.AmbientColor;
    m_lights.GroundBounce = skyLights.GroundBounce;
    m_lights.HazeColor    = skyLights.HazeColor;
}

void Wotah::App::ApplyPhysicsSettings()
{
    // Gravity and the ball's surface. Time speed and field of view are read every frame.
    State&      state       = *m_state;
    BodySurface ballSurface = RubberSurface;
    ballSurface.Bounciness  = m_settings.BallBounciness;
    const bool isApplied =
        state.Physics.SetGravity(m_settings.Gravity) && state.Physics.SetSurface(state.BallBody, ballSurface);
    if (!isApplied)
    {
        Logger::Error("Could not put the settings into the physics");
    }
}

void Wotah::App::ApplyClicks(const ControlBarClicks& clicks)
{
    if (clicks.TogglePause)
    {
        m_isPaused = !m_isPaused;
    }
    if (clicks.ResetCamera)
    {
        FocusActiveCamera();
    }
    if (clicks.ResetScene)
    {
        ResetScene(); // paused or not stays as it was, so a paused scene can be reset and then watched again
    }
}

void Wotah::App::ResetScene()
{
    // Only the ball goes back, in the world as it is: nothing is made again, so no frame waits for it (making the
    // world again, floor included, held a frame up). Gravity and the ball's bounce stay as set; so does the time of
    // day.
    State&     state   = *m_state;
    const bool isReset = state.Physics.PlaceBody(state.BallBody, state.BallStart.Position, state.BallStart.Rotation);
    if (!isReset)
    {
        Logger::Error("Could not reset the scene");
    }
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
    // Children before their parents: the objects made on the device (a fresh State destroys the old one, the UI's
    // renderer first), the UI it was attached to, the device, the window, then the log.
    m_state = std::make_unique<State>();
    m_ui.Destroy();
    m_device.Destroy();
    m_window.Destroy();
    Logger::Destroy(); // last, so everything above can still log
}
