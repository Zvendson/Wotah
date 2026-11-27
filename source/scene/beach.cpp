/**
 * @file beach.cpp
 * The example scene's start view, settings and physics.
 */
#include "scene/beach.hpp"

#include "core/logger.hpp"
#include "core/utils.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
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
     * the water is about a unit deep, in front of the start camera. It floats like a hollow rubber ball, a third of it
     * under the water (BodySettings::Buoyancy).
     */
    constexpr glm::vec2 BallSpot{-16.0f, 0.0f};
    constexpr float     BallDropHeight = 4.0f;
    constexpr float     BallBuoyancy   = 3.0f;

    /**
     * Where DropBall looks for land or water along its ray: up to DropReach metres out, in steps of half the height the
     * ray still is over them (so land rising at up to 45 degrees towards a ray going down as steeply is not passed
     * through), at least DropSearchStep (finer than any bump a ball would notice), then DropRefineSteps halvings of the
     * last step (to a few millimetres). A ray that meets neither drops the ball DropAhead metres ahead, over the
     * ground.
     */
    constexpr float DropReach       = 300.0f;
    constexpr float DropSearchStep  = 0.5f;
    constexpr float DropStepShare   = 0.5f;
    constexpr int   DropRefineSteps = 8;
    constexpr float DropAhead       = 15.0f;

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

    /**
     * The waves: rolling in from the open sea in the west-south-west onto the beach, 0.5 m high (significant height,
     * trough to crest) as the scene starts: a calm day's swell. By eye.
     */
    constexpr glm::vec2 WaveHeading{1.0f, -0.2f};
    constexpr float     StartWaveHeight = 0.5f;

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

namespace
{
    /** How high the land or the water is at place (x, z), whichever is higher: what a dropped ball lands on. */
    float GetLandingHeight(const Wotah::Sea& sea, const glm::vec2& place)
    {
        const float land = Wotah::GetTerrainHeight(Wotah::BeachLand, place);
        return std::max(land, Wotah::FindWaterHeight(sea, place.x, place.y).value_or(sea.Level));
    }

    /** How far point lies over the land or the water; below 0 under them. */
    float GetLandingGap(const Wotah::Sea& sea, const glm::vec3& point)
    {
        return point.y - GetLandingHeight(sea, {point.x, point.z});
    }

    /**
     * Where a ray (from origin along direction, of length 1) first meets the land or the water: stepped along until it
     * is under them, then the last step halved until it lies on them. The origin itself when it is under them already;
     * empty when the ray meets neither within reach.
     */
    std::optional<glm::vec3> FindLanding(const Wotah::Sea& sea, const glm::vec3& origin, const glm::vec3& direction)
    {
        float gap = GetLandingGap(sea, origin);
        if (gap < 0.0f)
        {
            return origin;
        }
        float above = 0.0f;
        while (above < DropReach)
        {
            const float distance = std::min(above + std::max(DropSearchStep, DropStepShare * gap), DropReach);
            gap                  = GetLandingGap(sea, origin + direction * distance);
            if (gap < 0.0f)
            {
                float below = distance;
                for (int halving = 0; halving < DropRefineSteps; ++halving)
                {
                    const float middle = (above + below) / 2.0f;
                    if (GetLandingGap(sea, origin + direction * middle) < 0.0f)
                    {
                        below = middle;
                    }
                    else
                    {
                        above = middle;
                    }
                }
                return origin + direction * below;
            }
            above = distance;
        }
        return std::nullopt;
    }

    /** Where a ray that meets nothing drops a ball: DropAhead metres ahead of origin along its way over the ground. */
    glm::vec3 GetSpotAhead(const glm::vec3& origin, const glm::vec3& direction)
    {
        const glm::vec2 flat{direction.x, direction.z};
        const float     length = glm::length(flat);
        return length > 0.0f ? origin + glm::vec3(flat.x, 0.0f, flat.y) * (DropAhead / length) : origin;
    }
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
    settings.WaveHeight           = StartWaveHeight;
    settings.FieldOfViewDegrees   = glm::degrees(Camera{}.GetFieldOfView()); // every camera's default
    return settings;
}

bool Wotah::BeachWorld::Create()
{
    Destroy(); // a second Create replaces the first

    // The land collides where it is drawn, from the same formula, wherever the balls and the camera go (Step); the
    // sea lies still until the settings bring its waves. Room for every ball, so adding one never allocates.
    m_sea         = BeachSea;
    m_ballSurface = RubberSurface;
    m_balls.reserve(BeachMaxBalls);
    m_solidAround.reserve(BeachMaxBalls + 1);
    if (!m_physics.Create() || !m_solidLand.Create(m_physics, BeachLand, SandSurface) || !m_physics.SetSea(m_sea))
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
    ball.Surface     = m_ballSurface;
    ball.SpinDamping = BallSpinDamping;
    ball.MoveDamping = BallMoveDamping;
    ball.Buoyancy    = BallBuoyancy;
    if (!AddBall(ball))
    {
        return false;
    }
    m_ballStart = ball;
    m_solidLand.Follow(std::span(&ball.Position, 1));
    m_solidLand.FinishPending();
    return true;
}

void Wotah::BeachWorld::Destroy()
{
    m_solidLand.Destroy();
    m_physics.Destroy();
    m_balls.clear();
    m_ballStart   = BodySettings{};
    m_ballSurface = BodySurface{};
    m_sea         = BeachSea;
}

void Wotah::BeachWorld::Step(float seconds, const glm::vec3& viewer)
{
    // The land is solid around the balls and the viewer wherever they went, before the world moves on.
    m_solidAround.clear();
    for (std::size_t ball = 0; ball < m_balls.size(); ++ball)
    {
        m_solidAround.emplace_back(GetBallTransform(ball)[3]);
    }
    m_solidAround.push_back(viewer);
    m_solidLand.Follow(m_solidAround);
    m_physics.Step(seconds);
}

bool Wotah::BeachWorld::Reset()
{
    // Only the balls change, in the world as it is: nothing is made again, so no frame waits for it (making the world
    // again, floor included, held a frame up). Gravity and the balls' bounce stay as set.
    if (m_balls.empty())
    {
        Logger::Error("Could not reset the scene: it was not created");
        return false;
    }
    bool isReset = true;
    while (m_balls.size() > 1)
    {
        isReset = m_physics.RemoveBody(m_balls.back()) && isReset; // logged
        m_balls.pop_back();
    }
    if (!m_physics.PlaceBody(m_balls.front(), m_ballStart.Position, m_ballStart.Rotation) || !isReset)
    {
        Logger::Error("Could not reset the scene");
        return false;
    }
    return true;
}

bool Wotah::BeachWorld::ApplySettings(const SceneSettings& settings)
{
    m_ballSurface            = RubberSurface;
    m_ballSurface.Bounciness = settings.BallBounciness;
    m_sea.Waves              = MakeSeaWaves(settings.WaveHeight, WaveHeading);
    bool isApplied           = m_physics.SetGravity(settings.Gravity) && m_physics.SetSea(m_sea);
    for (const BodyId ball : m_balls)
    {
        isApplied = m_physics.SetSurface(ball, m_ballSurface) && isApplied;
    }
    if (!isApplied)
    {
        Logger::Error("Could not put the settings into the physics");
        return false;
    }
    return true;
}

bool Wotah::BeachWorld::DropBall(const glm::vec3& origin, const glm::vec3& direction)
{
    if (m_balls.empty())
    {
        Logger::Error("Could not drop a ball: the scene was not created");
        return false;
    }
    if (m_balls.size() >= BeachMaxBalls)
    {
        Logger::Warning("The beach holds {} balls at most", BeachMaxBalls);
        return false;
    }

    // Over where the ray meets land or water (as they stand now), as high above them as the first ball starts, still
    // and upright, with the first ball's damping and floating and every ball's surface.
    const Sea       sea   = GetSea();
    const glm::vec3 spot  = FindLanding(sea, origin, direction).value_or(GetSpotAhead(origin, direction));
    const float     below = GetLandingHeight(sea, {spot.x, spot.z});
    BodySettings    ball  = m_ballStart;
    ball.Position         = {spot.x, below + BallDropHeight + BeachBallRadius, spot.z};
    ball.Rotation         = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    ball.Surface          = m_ballSurface;
    return AddBall(ball);
}

glm::mat4 Wotah::BeachWorld::GetBallTransform(std::size_t ball) const
{
    // Every listed ball is in the world; identity for any other.
    if (ball >= m_balls.size())
    {
        return glm::mat4(1.0f);
    }
    return m_physics.FindTransform(m_balls[ball]).value_or(glm::mat4(1.0f));
}

std::optional<std::size_t> Wotah::BeachWorld::FindBallAt(const glm::vec3& origin, const glm::vec3& direction) const
{
    std::optional<std::size_t> nearest;
    float                      nearestDistance = 0.0f;
    for (std::size_t ball = 0; ball < m_balls.size(); ++ball)
    {
        const glm::vec3            middle = GetBallTransform(ball)[3];
        const std::optional<float> hit    = FindRaySphereHit(origin, direction, middle, BeachBallRadius);
        if (hit && (!nearest || *hit < nearestDistance))
        {
            nearest         = ball;
            nearestDistance = *hit;
        }
    }
    return nearest;
}

bool Wotah::BeachWorld::PlaceBall(std::size_t ball, const glm::vec3& position)
{
    if (ball >= m_balls.size())
    {
        Logger::Error("There is no ball {}; there are {}", ball, m_balls.size());
        return false;
    }
    // Turned as it is drawn now, so it does not jump round.
    const glm::quat turn = glm::quat_cast(glm::mat3(GetBallTransform(ball)));
    return m_physics.PlaceBody(m_balls[ball], position, turn); // logged
}

bool Wotah::BeachWorld::AddBall(const BodySettings& settings)
{
    const std::optional<BodyId> ball = m_physics.AddSphere(BeachBallRadius, settings);
    if (!ball)
    {
        return false; // logged
    }
    m_balls.push_back(*ball);
    return true;
}

Wotah::Sea Wotah::BeachWorld::GetSea() const
{
    // The waves at the world's clock as drawn, so the ball floats on the very surface that is drawn.
    Sea sea     = m_sea;
    sea.Seconds = m_physics.GetSeconds();
    return sea;
}
