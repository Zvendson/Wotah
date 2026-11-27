/**
 * @file beach.cpp
 * The example scene's start view, settings and physics.
 */
#include "scene/beach.hpp"

#include "core/logger.hpp"

#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <optional>
#include <span>

namespace
{
    /** Where the start camera stands (x and z) and how high above the sand, and what it looks at (BeachCamera). */
    constexpr glm::vec2 StartCameraSpot{12.0f, 14.0f};
    constexpr float     StartCameraHeight = 4.0f;
    constexpr glm::vec3 StartCameraTarget{-17.0f, 0.0f, 6.0f};

    /**
     * The nearest and farthest distance the start camera sees: far enough for the land and sea to reach the haze,
     * which hides everything past a few hundred units.
     */
    constexpr float StartCameraNear = 0.1f;
    constexpr float StartCameraFar  = 2000.0f;

    /**
     * Where the ball is dropped: from 4 units above the sea (its lowest point) 16 units out from the waterline, where
     * the water is about a unit deep, in front of the start camera.
     */
    constexpr glm::vec2 BallSpot{-16.0f, 0.0f};
    constexpr float     BallDropHeight = 4.0f;

    /**
     * The surfaces, chosen by eye: sand grips and hardly bounces, the ball is like rubber. Two bodies in contact bounce
     * like the bouncier one, so the ball bounces on the sand.
     */
    constexpr Wotah::BodySurface SandSurface{0.8f, 0.1f};
    constexpr Wotah::BodySurface RubberSurface{0.7f, 0.6f};

    /**
     * How fast the ball rolling on the sand slows down by itself, per second: the sand's rolling resistance, which the
     * physics has no other way to know. Chosen by testing: a ball knocked aside rolls a metre or two.
     */
    constexpr float BallSpinDamping = 1.0f;
    constexpr float BallMoveDamping = 0.2f;

    /** How bright the sun is as the scene starts (the Sun brightness setting); chosen by eye. */
    constexpr float SunIntensity = 0.9f;

    /**
     * The day as the scene starts: 9 in the morning, the sun in the east-south-east about 38 degrees up, behind the
     * start view, so the ball's shadow falls away from the camera; a day that reaches 60 degrees at noon (about 30
     * degrees north, as in Cairo at an equinox) and lasts 10 minutes, so a sunset shows within a few minutes.
     */
    constexpr float StartHours        = 9.0f;
    constexpr float NoonHeightDegrees = 60.0f;
    constexpr float DayLengthMinutes  = 10.0f;

    /**
     * Where the settings start beyond the values above: time runs as fast as the clock, the day runs, sky and moon give
     * all their light.
     */
    constexpr float RealTimeSpeed = 1.0f;
    constexpr float FullSkyLight  = 1.0f;
    constexpr float FullMoonLight = 1.0f;
} // namespace

Wotah::Camera Wotah::MakeBeachCamera()
{
    // At its height above the sand wherever that is, so a changed land shape cannot bury it.
    Camera      camera;
    const float ground = GetTerrainHeight(BeachLand, StartCameraSpot);
    camera.SetPosition({StartCameraSpot.x, ground + StartCameraHeight, StartCameraSpot.y});
    camera.SetTarget(StartCameraTarget);
    (void)camera.SetClipDistances(StartCameraNear, StartCameraFar); // valid values: 0 < near < far, both finite
    return camera;
}

Wotah::SceneSettings Wotah::MakeBeachSettings()
{
    SceneSettings settings;
    settings.TimeOfDayHours       = StartHours;
    settings.IsDayRunning         = true;
    settings.DayLengthMinutes     = DayLengthMinutes;
    settings.SunNoonHeightDegrees = NoonHeightDegrees;
    settings.SunBrightness        = SunIntensity;
    settings.MoonBrightness       = FullMoonLight;
    settings.SkyLight             = FullSkyLight;
    settings.HasShadows           = true;
    settings.Gravity              = StandardGravity;
    settings.TimeSpeed            = RealTimeSpeed;
    settings.BallBounciness       = RubberSurface.Bounciness;
    settings.FieldOfViewDegrees   = glm::degrees(Camera{}.GetFieldOfView()); // every camera's default
    return settings;
}

bool Wotah::BeachWorld::Create()
{
    Destroy(); // a second Create replaces the first

    // The land collides where it is drawn, from the same formula, wherever the ball and the camera go (Step).
    if (!m_physics.Create() || !m_solidLand.Create(m_physics, BeachLand, SandSurface))
    {
        return false;
    }

    // The ball starts above the sea; the land around it is made before the first step, so it lands on something.
    const std::optional<float> seaHeight = FindWaterHeight(BeachSea, BallSpot.x, BallSpot.y);
    if (!seaHeight)
    {
        Logger::Error("The ball must start above the sea");
        return false;
    }
    BodySettings ball;
    ball.Position    = {BallSpot.x, *seaHeight + BallDropHeight + BeachBallRadius, BallSpot.y};
    ball.Surface     = RubberSurface;
    ball.SpinDamping = BallSpinDamping;
    ball.MoveDamping = BallMoveDamping;

    const std::optional<BodyId> ballBody = m_physics.AddSphere(BeachBallRadius, ball);
    if (!ballBody)
    {
        return false;
    }
    m_ball      = *ballBody;
    m_ballStart = ball;
    m_solidLand.Follow(std::span(&ball.Position, 1));
    m_solidLand.FinishPending();
    return true;
}

void Wotah::BeachWorld::Destroy()
{
    m_solidLand.Destroy();
    m_physics.Destroy();
    m_ball      = BodyId{};
    m_ballStart = BodySettings{};
}

void Wotah::BeachWorld::Step(float seconds, const glm::vec3& viewer)
{
    // The land is solid around the ball and the viewer wherever they went, before the world moves on.
    const std::array<glm::vec3, 2> solidAround{glm::vec3(GetBallTransform()[3]), viewer};
    m_solidLand.Follow(solidAround);
    m_physics.Step(seconds);
}

bool Wotah::BeachWorld::Reset()
{
    // Only the ball goes back, in the world as it is: nothing is made again, so no frame waits for it (making the
    // world again, floor included, held a frame up). Gravity and the ball's bounce stay as set.
    if (!m_physics.PlaceBody(m_ball, m_ballStart.Position, m_ballStart.Rotation))
    {
        Logger::Error("Could not reset the scene");
        return false;
    }
    return true;
}

bool Wotah::BeachWorld::ApplySettings(const SceneSettings& settings)
{
    BodySurface ballSurface = RubberSurface;
    ballSurface.Bounciness  = settings.BallBounciness;
    if (!m_physics.SetGravity(settings.Gravity) || !m_physics.SetSurface(m_ball, ballSurface))
    {
        Logger::Error("Could not put the settings into the physics");
        return false;
    }
    return true;
}

glm::mat4 Wotah::BeachWorld::GetBallTransform() const
{
    // Always found once Create added the ball; identity before.
    return m_physics.FindTransform(m_ball).value_or(glm::mat4(1.0f));
}
