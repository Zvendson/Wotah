/**
 * @file app.cpp
 * The application: wires window, device, renderer, scene and UI together and runs the frames. The scene is the beach
 * (scene/beach.hpp): land and sea to the horizon, a blue ball dropped into the shallow water, a day that runs; the
 * scene renderer (render/scene_renderer.hpp) draws it, the ball is a mesh with its own look. Over the scene lie a small
 * stats window, a list of the controls and a control bar (Dear ImGui) that pauses the world and resets the scene or
 * the camera; R resets the scene too. Tab opens the settings window.
 */
#include "app.hpp"

#include "core/logger.hpp"
#include "input/input_event.hpp"
#include "render/mesh.hpp"
#include "render/mesh_data.hpp"
#include "render/mesh_pipelines.hpp"
#include "render/renderer.hpp"
#include "render/scene_renderer.hpp"
#include "render/sky.hpp"
#include "render/sky_clock.hpp"
#include "render/sky_lights.hpp"
#include "render/swapchain.hpp"
#include "render/ui_renderer.hpp"
#include "scene/beach.hpp"
#include "ui/control_bar.hpp"
#include "ui/hints_window.hpp"
#include "ui/settings_window.hpp"
#include "ui/stats_window.hpp"

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <thread>

namespace
{
    /** The clock frame times are measured with: steady, so changing the system time cannot make a frame negative. */
    using Clock = std::chrono::steady_clock;

    /**
     * The longest frame time the camera and the world move by: after a stall (the window minimized, a breakpoint) they
     * continue from where they were instead of jumping far.
     */
    constexpr float MaxFrameSeconds = 0.1f;

    /** The ball's fragment shader (shaders/ball.frag), compiled to SPIR-V by the build. */
    constexpr uint32_t BallFragmentShader[] = {
#include "ball.frag.inc"
    };

    /**
     * The window's size at the start, in pixels: 1280 x 720 (HD, 16:9). It fits on nearly every screen, small 1366 x
     * 768 laptops too, and leaves the scene room beside the UI's cards.
     */
    constexpr Wotah::WindowSize MainWindowSize{1280, 720};

    /** The window's title and the application name Vulkan is told. */
    constexpr const char* AppName = "Wotah";

    /** What a frame starts with; the sky covers all of it right away, so this only shows if the sky is missing. */
    constexpr VkClearColorValue BackgroundColor{{0.1f, 0.1f, 0.1f, 1.0f}};

    /**
     * Where shadow casters can stand: a sphere around the camera, wide enough for hills well beyond the shadows' reach
     * (SceneLights::ShadowReach) to still throw their shadows into it when the sun is low.
     */
    constexpr float ShadowCasterRadius = 150.0f;

    /** How finely the ball is cut: enough for a smooth outline at its size. */
    constexpr uint32_t BallSegments = 48;
    constexpr uint32_t BallRings    = 24;
} // namespace

/**
 * What the application makes on top of the device, and the world. Declared in creation order, so they are destroyed
 * in reverse: the UI's renderer, the world, the ball and the scene first, the renderer before the swapchain it draws
 * into.
 */
struct Wotah::App::State
{
    /** The swapchain, its images and one view per image. */
    Wotah::Swapchain Swapchain;

    /** The command buffer, depth image and synchronization of each frame. */
    Wotah::Renderer Renderer;

    /** The lights, sky, land and sea on the GPU, and how a frame of them is drawn. */
    Wotah::SceneRenderer Scene;

    /** The blue ball's look (the scene's and the mirror's pipeline) and shape. */
    Wotah::MeshPipelines BallLook;
    Wotah::Mesh          Ball;

    /** The beach's physics: the land solid around the ball and the camera, and the ball. */
    Wotah::BeachWorld World;

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

    // The renderer comes before the scene: it draws into the renderer's frames and reads the renderer's copies.
    if (!m_window.Create(MainWindowSize, AppName) || !m_device.Create(m_window, AppName) ||
        !state.Swapchain.Create(m_device, m_window) || !state.Renderer.Create(m_device, state.Swapchain))
    {
        return false;
    }
    state.Renderer.SetClearColor(BackgroundColor);

    // The scene, then the ball's look, which reads the scene's lights; the ball's shape goes to GPU-only memory once
    // (Create waits until it is there). The world drops the ball and makes the land under it solid.
    const VkFormat colorFormat = state.Swapchain.GetFormat();
    const VkFormat depthFormat = state.Renderer.GetDepthFormat();
    if (!state.Scene.Create(m_device, colorFormat, state.Renderer, BeachLand) ||
        !state.BallLook.Create(m_device, BallFragmentShader, colorFormat, depthFormat, state.Scene.GetLighting()) ||
        !state.Ball.Create(m_device, MakeSphere(BeachBallRadius, BallSegments, BallRings)) || !state.World.Create())
    {
        return false;
    }

    // The UI over the scene: ImGui first, then its renderer, which attaches to it and draws into the same frames.
    if (!m_ui.Create() || !state.UiRenderer.Create(m_device, colorFormat, depthFormat))
    {
        return false;
    }

    // The first camera shows the beach's start view; Run keeps every active camera's aspect ratio in step with the
    // image.
    m_cameras.front()    = MakeBeachCamera();
    m_focusViews.front() = m_cameras.front();

    // The settings start as the scene starts; putting them in once makes them the one source from now on (and puts
    // the sun and the moon into the lights).
    m_settings = MakeBeachSettings();
    ApplySettings();

    // The UI is asked first and keeps what lands on its windows; the camera controller and the scene's keys get the
    // rest.
    m_input.AddHandler(m_ui);
    m_input.AddHandler(m_cameraController);
    m_input.AddHandler(m_sceneShortcuts);
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
    State&    state    = *m_state;
    Renderer& renderer = state.Renderer;
    HandleInput(frameSeconds);
    AdvanceWorld(frameSeconds);

    // A new window size, or a swapchain the system no longer finds fitting: make it again before drawing. While the
    // user drags the window's edge this happens every few frames, so the picture follows the edge.
    if (m_window.TakeResize() || state.Swapchain.IsOutOfDate())
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
    const VkExtent2D extent = state.Swapchain.GetExtent();
    Camera&          camera = GetActiveCamera();
    (void)camera.SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    // And the settings' field of view, also after F put a camera back to its focus view; the slider's range lies well
    // inside the 0 to 180 degrees a camera takes, so it always fits.
    (void)camera.SetFieldOfView(glm::radians(m_settings.FieldOfViewDegrees));

    // Shadow casters stand around the camera, wherever it goes; the ball stands where the physics has it, the waves
    // where the ball sees them.
    m_lights.Shadows = m_settings.HasShadows ? ShadowArea{camera.GetPosition(), ShadowCasterRadius} : ShadowArea{};
    const std::array<MeshObject, 1> objects{{{&state.BallLook, &state.Ball, state.World.GetBallTransform()}}};
    state.Scene.Draw(renderer, camera, m_lights, state.World.GetSea(), objects);

    // The UI last, so it lies over everything.
    return state.UiRenderer.Draw(renderer.GetCommandBuffer()) && renderer.EndFrame();
}

void Wotah::App::HandleInput(float frameSeconds)
{
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
        (void)m_state->World.Reset(); // logged; paused or not stays as it was
    }
    if (m_sceneShortcuts.TakeSettingsRequest())
    {
        m_isSettingsOpen = !m_isSettingsOpen;
    }
    m_window.RequestCursorCaptured(m_cameraController.IsTurning());
    m_cameraController.Update(GetActiveCamera(), frameSeconds);
}

void Wotah::App::AdvanceWorld(float frameSeconds)
{
    if (!m_isPaused)
    {
        const float worldSeconds = frameSeconds * m_settings.TimeSpeed;
        m_state->World.Step(worldSeconds, GetActiveCamera().GetPosition());
        if (m_settings.IsDayRunning)
        {
            m_settings.TimeOfDayHours =
                AdvanceTimeOfDay(m_settings.TimeOfDayHours, worldSeconds, m_settings.DayLengthMinutes);
            ApplySkySettings();
        }
    }

    // The sun's rays and glow and the stars move a little in real time, paused or not, like the camera.
    m_lights.SkySeconds = std::fmod(m_lights.SkySeconds + frameSeconds, SkyLoopSeconds);
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
        const SceneSettings defaults = MakeBeachSettings();
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
    (void)m_state->World.ApplySettings(m_settings); // logged
}

void Wotah::App::ApplySkySettings()
{
    // The sky's lights at this time of day (render/sky_lights.hpp): sun and moon where the clock has them, with their
    // light, the disks for the sky, the sky's light, the haze and the light the ground throws back.
    SkyLightSettings sky;
    sky.TimeOfDayHours = m_settings.TimeOfDayHours;
    sky.NoonElevation  = glm::radians(m_settings.SunNoonHeightDegrees);
    sky.SunBrightness  = m_settings.SunBrightness;
    sky.MoonBrightness = m_settings.MoonBrightness;
    sky.SkyLight       = m_settings.SkyLight;
    PutSkyLights(MakeSkyLights(sky), m_lights);
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
        // Paused or not stays as it was, so a paused scene can be reset and then watched again; the time of day stays.
        (void)m_state->World.Reset(); // logged
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
