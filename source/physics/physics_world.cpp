/**
 * @file physics_world.cpp
 * The physics world on top of Jolt Physics 5.6: starting Jolt once for every world, the layers that say what collides
 * with what, adding bodies, fixed steps, and blending the last two steps for drawing.
 */
#include "physics/physics_world.hpp"

#include "core/logger.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace
{
    // ---- How big a world can get --------------------------------------------------------------------------------
    // Far more than the scene needs now; Jolt reserves room for all of it up front (Jolt docs, PhysicsSystem::Init).

    /** The most bodies one world holds. */
    constexpr uint32_t MaxBodies = 1024;
    /** How many locks guard the bodies; 0 lets Jolt pick a default. */
    constexpr uint32_t BodyLockCount = 0;
    /** The most body pairs close enough to maybe touch in one step. */
    constexpr uint32_t MaxBodyPairs = 1024;
    /** The most contacts between bodies in one step. */
    constexpr uint32_t MaxContacts = 1024;
    /** Scratch memory for one step: 10 MiB, as in Jolt's own examples (HelloWorld.cpp). */
    constexpr uint32_t ScratchBytes = 10 * 1024 * 1024;
    /** Collision checks per step: 1 is enough at 60 steps a second (Jolt docs, PhysicsSystem::Update). */
    constexpr int CollisionChecksPerStep = 1;

    // ---- What collides with what ----------------------------------------------------------------------------------

    /** Fixed bodies never collide with each other, so they get their own layer; moving ones collide with everything. */
    constexpr JPH::ObjectLayer FixedLayer       = 0;
    constexpr JPH::ObjectLayer MovingLayer      = 1;
    constexpr uint32_t         ObjectLayerCount = 2;

    /** The broad phase (the quick "could these touch at all" check) keeps the same split. */
    constexpr JPH::BroadPhaseLayer FixedBroadPhase{0};
    constexpr JPH::BroadPhaseLayer MovingBroadPhase{1};
    constexpr uint32_t             BroadPhaseLayerCount = 2;

    // ---- Fast bodies ---------------------------------------------------------------------------------------------

    /**
     * From which speed a body's whole way through a step is checked for hits (LinearCast), as a share of its inner
     * radius moved per step. Jolt's 0.75 starts only at 22 units a second for the program's balls, so a ball dropped
     * from 2.5 units (7 units a second) sank 7.7 cm into the ground before it was pushed out; 0.1 starts at 3 units a
     * second.
     */
    constexpr float FastBodyShare = 0.1f;

    /** How deep a fast body may still end up in another, as a share of its inner radius: 2.5 cm for the balls. */
    constexpr float FastBodyMaxSinkShare = 0.05f;

    /**
     * How deep resting bodies may sink into each other: 5 mm. Jolt's 2 cm left a ball lying visibly in the ground; less
     * than a few millimetres can make resting bodies jitter (Jolt docs, PhysicsSettings::mPenetrationSlop).
     */
    constexpr float RestingSink = 0.005f;

    /** Below this squared length a quaternion counts as zero, so it names no rotation. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    // ---- Jolt, started once for all worlds -----------------------------------------------------------------------

    /** Guards starting and stopping Jolt, which is global: its allocator, its type factory and its type list. */
    std::mutex s_joltMutex;
    /** How many worlds use Jolt; it starts with the first and stops after the last. */
    uint32_t s_joltUsers = 0;
    /** Jolt's type factory; Jolt reads it through Factory::sInstance. */
    std::unique_ptr<JPH::Factory> s_factory;

    /** Jolt's messages (printf style) go to the log. */
    void LogJoltMessage(const char* format, ...)
    {
        constexpr std::size_t         MessageSize = 1024;
        std::array<char, MessageSize> message{};
        va_list                       arguments;
        va_start(arguments, format);
        std::vsnprintf(message.data(), message.size(), format, arguments);
        va_end(arguments);
        Wotah::Logger::Debug("Jolt: {}", message.data());
    }

#ifdef JPH_ENABLE_ASSERTS
    /** A failed check inside Jolt is logged; false lets the program go on instead of stopping in a debugger. */
    bool LogJoltAssert(const char* expression, const char* message, const char* file, JPH::uint line)
    {
        Wotah::Logger::Error("Jolt check failed: {} ({}) at {}:{}", expression, message != nullptr ? message : "", file,
                             line);
        return false;
    }
#endif

    /** Starts Jolt for one more world. */
    void StartJolt()
    {
        const std::scoped_lock lock(s_joltMutex);
        if (s_joltUsers++ > 0)
        {
            return;
        }
        // The allocator first: everything after it allocates through it (Jolt docs, "Getting started").
        JPH::RegisterDefaultAllocator();
        JPH::Trace = LogJoltMessage;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = LogJoltAssert;)
        s_factory               = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = s_factory.get();
        JPH::RegisterTypes();
    }

    /** Stops Jolt when the last world is done with it. */
    void StopJolt()
    {
        const std::scoped_lock lock(s_joltMutex);
        if (--s_joltUsers > 0)
        {
            return;
        }
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
        s_factory.reset();
    }

    // ---- Between glm and Jolt ------------------------------------------------------------------------------------

    JPH::Vec3 ToJolt(const glm::vec3& vector)
    {
        return {vector.x, vector.y, vector.z};
    }

    JPH::Quat ToJolt(const glm::quat& rotation)
    {
        return {rotation.x, rotation.y, rotation.z, rotation.w};
    }

    glm::vec3 FromJolt(JPH::Vec3Arg vector)
    {
        return {vector.GetX(), vector.GetY(), vector.GetZ()};
    }

    glm::quat FromJolt(JPH::QuatArg rotation)
    {
        return {rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()};
    }

    JPH::EMotionType ToJolt(Wotah::BodyMotion motion)
    {
        switch (motion)
        {
            case Wotah::BodyMotion::Static:
                return JPH::EMotionType::Static;
            case Wotah::BodyMotion::Kinematic:
                return JPH::EMotionType::Kinematic;
            case Wotah::BodyMotion::Dynamic:
                return JPH::EMotionType::Dynamic;
        }
        return JPH::EMotionType::Static;
    }

    // ---- Checks ----------------------------------------------------------------------------------------------------

    bool IsFinite(const glm::vec3& vector)
    {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
    }

    /** Whether a body's start fits: a place and a rotation that are numbers, a rotation that names one. */
    bool AreSettingsUsable(const Wotah::BodySettings& settings)
    {
        const glm::quat& rotation = settings.Rotation;
        const bool       isUsable = IsFinite(settings.Position) && std::isfinite(rotation.w) &&
                                    IsFinite({rotation.x, rotation.y, rotation.z}) &&
                                    glm::dot(rotation, rotation) >= NearlyZeroSquared;
        if (!isUsable)
        {
            Wotah::Logger::Error("A body needs a position and a rotation made of numbers");
        }
        return isUsable;
    }
} // namespace

/**
 * Jolt's objects for one world. Declared in the order they depend on each other: the layer tables before the system,
 * which keeps references to them.
 */
struct Wotah::PhysicsWorld::State
{
    /** A body added to the world, and where it was at the last two steps, for blending. */
    struct TrackedBody
    {
        JPH::BodyID Id;
        BodyMotion  Motion = BodyMotion::Static;
        glm::vec3   PreviousPosition{0.0f};
        glm::quat   PreviousRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3   Position{0.0f};
        glm::quat   Rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    JPH::TempAllocatorImpl       Scratch{ScratchBytes};
    JPH::JobSystemSingleThreaded Jobs{JPH::cMaxPhysicsJobs};

    JPH::BroadPhaseLayerInterfaceTable                     BroadPhaseLayers{ObjectLayerCount, BroadPhaseLayerCount};
    JPH::ObjectLayerPairFilterTable                        LayerPairs{ObjectLayerCount};
    std::optional<JPH::ObjectVsBroadPhaseLayerFilterTable> LayerVsBroadPhase; // made from the two above, once set up

    JPH::PhysicsSystem System;

    /** Every body added, with its last two steps' places. */
    std::vector<TrackedBody> Bodies;

    /** Time handed to Step that did not fill a whole step yet. */
    float CarriedSeconds = 0.0f;

    /** The body behind an id; null for an unknown one. */
    [[nodiscard]] TrackedBody* Find(BodyId body)
    {
        const auto found = std::find_if(Bodies.begin(), Bodies.end(), [body](const TrackedBody& tracked)
                                        { return tracked.Id.GetIndexAndSequenceNumber() == body.Value; });
        return found != Bodies.end() ? &*found : nullptr;
    }

    /** Adds a body of shape and remembers it; empty (and logged) when the shape failed or the world is full. */
    std::optional<BodyId> AddBody(const JPH::ShapeSettings& shape, const BodySettings& settings);
};

std::optional<Wotah::BodyId> Wotah::PhysicsWorld::State::AddBody(const JPH::ShapeSettings& shape,
                                                                 const BodySettings&       settings)
{
    const JPH::ShapeSettings::ShapeResult made = shape.Create();
    if (made.HasError())
    {
        Logger::Error("Could not make a physics shape: {}", made.GetError().c_str());
        return std::nullopt;
    }

    const glm::quat           rotation = glm::normalize(settings.Rotation);
    const bool                isFixed  = settings.Motion == BodyMotion::Static;
    JPH::BodyCreationSettings body(made.Get(), JPH::RVec3(ToJolt(settings.Position)), ToJolt(rotation),
                                   ToJolt(settings.Motion), isFixed ? FixedLayer : MovingLayer);
    body.mFriction       = settings.Surface.Friction;
    body.mRestitution    = settings.Surface.Bounciness;
    body.mLinearDamping  = settings.MoveDamping;
    body.mAngularDamping = settings.SpinDamping;
    if (settings.Motion == BodyMotion::Dynamic)
    {
        // Checks the whole way it moved in a step, so a fast ball never passes through the ground between two steps.
        body.mMotionQuality = JPH::EMotionQuality::LinearCast;
    }

    const JPH::BodyID id = System.GetBodyInterfaceNoLock().CreateAndAddBody(
        body, isFixed ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid())
    {
        Logger::Error("The physics world is full ({} bodies)", MaxBodies);
        return std::nullopt;
    }

    TrackedBody tracked;
    tracked.Id               = id;
    tracked.Motion           = settings.Motion;
    tracked.PreviousPosition = settings.Position;
    tracked.PreviousRotation = rotation;
    tracked.Position         = settings.Position;
    tracked.Rotation         = rotation;
    Bodies.push_back(tracked);
    return BodyId{id.GetIndexAndSequenceNumber()};
}

Wotah::PhysicsWorld::PhysicsWorld() = default;

Wotah::PhysicsWorld::~PhysicsWorld()
{
    Destroy();
}

bool Wotah::PhysicsWorld::Create()
{
    Destroy(); // a second Create starts a new, empty world
    StartJolt();
    m_state = std::make_unique<State>();

    // Fixed bodies sit in their own layer and broad phase, and only moving ones collide with them.
    State& state = *m_state;
    state.BroadPhaseLayers.MapObjectToBroadPhaseLayer(FixedLayer, FixedBroadPhase);
    state.BroadPhaseLayers.MapObjectToBroadPhaseLayer(MovingLayer, MovingBroadPhase);
    state.LayerPairs.EnableCollision(MovingLayer, FixedLayer);
    state.LayerPairs.EnableCollision(MovingLayer, MovingLayer);
    state.LayerVsBroadPhase.emplace(state.BroadPhaseLayers, BroadPhaseLayerCount, state.LayerPairs, ObjectLayerCount);

    state.System.Init(MaxBodies, BodyLockCount, MaxBodyPairs, MaxContacts, state.BroadPhaseLayers,
                      *state.LayerVsBroadPhase, state.LayerPairs);
    state.System.SetGravity(JPH::Vec3(0.0f, -StandardGravity, 0.0f));

    JPH::PhysicsSettings tuning      = state.System.GetPhysicsSettings();
    tuning.mLinearCastThreshold      = FastBodyShare;
    tuning.mLinearCastMaxPenetration = FastBodyMaxSinkShare;
    tuning.mPenetrationSlop          = RestingSink;
    state.System.SetPhysicsSettings(tuning);
    return true;
}

void Wotah::PhysicsWorld::Destroy()
{
    if (!m_state)
    {
        return;
    }
    // The system removes and frees its bodies when it goes; Jolt stops after the last world is gone.
    m_state.reset();
    StopJolt();
}

std::optional<Wotah::BodyId> Wotah::PhysicsWorld::AddSphere(float radius, const BodySettings& settings)
{
    if (!m_state || !(radius > 0.0f) || !std::isfinite(radius) || !AreSettingsUsable(settings))
    {
        Logger::Error("A ball needs a created world and a radius above 0, not {}", radius);
        return std::nullopt;
    }
    return m_state->AddBody(JPH::SphereShapeSettings(radius), settings);
}

std::optional<Wotah::BodyId> Wotah::PhysicsWorld::AddBox(const glm::vec3& halfSize, const BodySettings& settings)
{
    if (!m_state || !(halfSize.x > 0.0f && halfSize.y > 0.0f && halfSize.z > 0.0f) || !IsFinite(halfSize) ||
        !AreSettingsUsable(settings))
    {
        Logger::Error("A box needs a created world and a size above 0, not {} x {} x {}", halfSize.x, halfSize.y,
                      halfSize.z);
        return std::nullopt;
    }
    // Jolt rounds a box's edges by a small radius for speed; it may not be larger than the box's smallest half size.
    const float rounding = std::min(JPH::cDefaultConvexRadius, std::min({halfSize.x, halfSize.y, halfSize.z}));
    return m_state->AddBody(JPH::BoxShapeSettings(ToJolt(halfSize), rounding), settings);
}

std::optional<Wotah::BodyId> Wotah::PhysicsWorld::AddTriangles(std::span<const glm::vec3> corners,
                                                               std::span<const uint32_t>  indices,
                                                               const BodySurface&         surface)
{
    const bool hasTriangles = !indices.empty() && indices.size() % 3 == 0;
    const bool areInside =
        std::all_of(indices.begin(), indices.end(), [&corners](uint32_t index) { return index < corners.size(); });
    if (!m_state || !hasTriangles || !areInside)
    {
        Logger::Error("Fixed triangles need a created world, three indices per triangle and only existing corners");
        return std::nullopt;
    }

    JPH::VertexList cornerList;
    cornerList.reserve(corners.size());
    for (const glm::vec3& corner : corners)
    {
        cornerList.push_back(JPH::Float3(corner.x, corner.y, corner.z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (std::size_t first = 0; first + 2 < indices.size(); first += 3)
    {
        triangles.push_back(JPH::IndexedTriangle(indices[first], indices[first + 1], indices[first + 2]));
    }

    BodySettings settings;
    settings.Motion  = BodyMotion::Static;
    settings.Surface = surface;
    const std::optional<BodyId> body =
        m_state->AddBody(JPH::MeshShapeSettings(std::move(cornerList), std::move(triangles)), settings);

    // Many triangles added at once: Jolt finds them faster after sorting its broad phase again (Jolt docs).
    m_state->System.OptimizeBroadPhase();
    return body;
}

bool Wotah::PhysicsWorld::SetVelocity(BodyId body, const glm::vec3& moving, const glm::vec3& spinning)
{
    const State::TrackedBody* tracked = m_state ? m_state->Find(body) : nullptr;
    if (tracked == nullptr || tracked->Motion == BodyMotion::Static || !IsFinite(moving) || !IsFinite(spinning))
    {
        Logger::Error("Only a known moving body can be given a velocity made of numbers");
        return false;
    }
    JPH::BodyInterface& bodies = m_state->System.GetBodyInterfaceNoLock();
    bodies.SetLinearAndAngularVelocity(tracked->Id, ToJolt(moving), ToJolt(spinning));
    bodies.ActivateBody(tracked->Id); // a resting body would ignore it until woken
    return true;
}

void Wotah::PhysicsWorld::Step(float seconds)
{
    if (!m_state)
    {
        return;
    }
    if (!(seconds >= 0.0f) || !std::isfinite(seconds))
    {
        Logger::Error("A physics step needs a time of 0 or more, not {}", seconds);
        return;
    }

    State& state = *m_state;
    state.CarriedSeconds += seconds;
    for (int step = 0; state.CarriedSeconds >= PhysicsStepSeconds; ++step)
    {
        if (step == MaxStepsPerCall)
        {
            // Catching up on more would take longer than the time it covers: drop it, the world runs slower briefly.
            state.CarriedSeconds = std::fmod(state.CarriedSeconds, PhysicsStepSeconds);
            break;
        }

        const JPH::EPhysicsUpdateError errors =
            state.System.Update(PhysicsStepSeconds, CollisionChecksPerStep, &state.Scratch, &state.Jobs);
        if (errors != JPH::EPhysicsUpdateError::None)
        {
            Logger::Warning("A physics step ran out of room (Jolt error bits {})", static_cast<uint32_t>(errors));
        }
        state.CarriedSeconds -= PhysicsStepSeconds;

        // The step before becomes the one to blend from.
        const JPH::BodyInterface& bodies = state.System.GetBodyInterfaceNoLock();
        for (State::TrackedBody& tracked : state.Bodies)
        {
            JPH::RVec3 position;
            JPH::Quat  rotation;
            bodies.GetPositionAndRotation(tracked.Id, position, rotation);
            tracked.PreviousPosition = tracked.Position;
            tracked.PreviousRotation = tracked.Rotation;
            tracked.Position         = FromJolt(JPH::Vec3(position));
            tracked.Rotation         = FromJolt(rotation);
        }
    }
}

std::optional<glm::mat4> Wotah::PhysicsWorld::FindTransform(BodyId body) const
{
    const State::TrackedBody* tracked = m_state ? m_state->Find(body) : nullptr;
    if (tracked == nullptr)
    {
        return std::nullopt;
    }
    // How far the carried-over time reaches into the next step: 0 shows the last step, nearly 1 almost the next.
    const float     between  = std::clamp(m_state->CarriedSeconds / PhysicsStepSeconds, 0.0f, 1.0f);
    const glm::vec3 position = glm::mix(tracked->PreviousPosition, tracked->Position, between);
    const glm::quat rotation = glm::slerp(tracked->PreviousRotation, tracked->Rotation, between);
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
}

std::optional<glm::vec3> Wotah::PhysicsWorld::FindVelocity(BodyId body) const
{
    const State::TrackedBody* tracked = m_state ? m_state->Find(body) : nullptr;
    if (tracked == nullptr)
    {
        return std::nullopt;
    }
    return FromJolt(m_state->System.GetBodyInterfaceNoLock().GetLinearVelocity(tracked->Id));
}
