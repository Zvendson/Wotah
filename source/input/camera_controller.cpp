/**
 * @file camera_controller.cpp
 * Which keys move the camera, and how mouse movement turns it.
 */
#include "input/camera_controller.hpp"

#include "input/input_event.hpp"
#include "render/camera.hpp"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    /** How fast the camera flies, in world units per second. */
    constexpr float MoveSpeed = 3.0f;

    /** How far the camera turns per pixel of mouse movement: 0.15 degrees. */
    constexpr float TurnPerPixel = glm::radians(0.15f);

    /** How far the camera may look up or down: 89 degrees, so it never looks straight along the up direction. */
    constexpr float MaxPitch = glm::radians(89.0f);

    /** The world's up direction. */
    constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};

    /** Below this squared length a direction counts as zero. */
    constexpr float NearlyZeroSquared = 1.0e-12f;

    /** -1, 0 or +1 from two opposite keys. */
    float Axis(bool positive, bool negative)
    {
        return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    }

    /** The view turned by a mouse movement in pixels, keeping its length; yaw around the up axis, then pitch. */
    glm::vec3 Turn(const glm::vec3& view, float distance, const glm::vec2& pixels)
    {
        const glm::vec3 direction = view / distance;
        float           yaw       = std::atan2(direction.z, direction.x);
        float           pitch     = std::asin(std::clamp(direction.y, -1.0f, 1.0f));

        // Mouse right turns right; mouse down (pixels grow downwards) looks down.
        yaw += pixels.x * TurnPerPixel;
        pitch = std::clamp(pitch - pixels.y * TurnPerPixel, -MaxPitch, MaxPitch);

        const glm::vec3 turned{std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
        return turned * distance;
    }
} // namespace

bool Wotah::CameraController::OnInput(const InputEvent& event)
{
    switch (event.Type)
    {
        case InputEventType::KeyPressed:
            return SetHeld(event.Key, true);
        case InputEventType::KeyReleased:
            return SetHeld(event.Key, false);
        case InputEventType::MouseButtonPressed:
        case InputEventType::MouseButtonReleased:
            if (event.Button != MouseButton::Right)
            {
                return false;
            }
            m_isTurning = event.Type == InputEventType::MouseButtonPressed;
            return true;
        case InputEventType::MouseMoved:
            if (!m_isTurning)
            {
                return false;
            }
            m_pendingTurn += event.Delta;
            return true;
        case InputEventType::FocusLost:
            ReleaseAll();
            return false;
        case InputEventType::MouseScrolled:
        case InputEventType::TextEntered:
            return false;
    }
    return false;
}

void Wotah::CameraController::Update(Camera& camera, float seconds)
{
    const glm::vec3 position = camera.GetPosition();
    glm::vec3       view     = camera.GetTarget() - position;
    const float     distance = glm::length(view);
    if (distance * distance < NearlyZeroSquared)
    {
        m_pendingTurn = glm::vec2(0.0f);
        return; // a camera on its target has no direction to move or turn along
    }

    const bool isTurned = m_pendingTurn != glm::vec2(0.0f);
    if (isTurned)
    {
        view          = Turn(view, distance, m_pendingTurn);
        m_pendingTurn = glm::vec2(0.0f);
    }

    // W/S along the view flattened onto the ground, A/D sideways, Space/Shift straight up and down.
    const float     forwardAmount = Axis(m_held.Forward, m_held.Backward);
    const float     rightAmount   = Axis(m_held.Right, m_held.Left);
    const float     upAmount      = Axis(m_held.Up, m_held.LeftShift || m_held.RightShift);
    const glm::vec3 ground{view.x, 0.0f, view.z};
    glm::vec3       move = WorldUp * upAmount;
    if (glm::dot(ground, ground) >= NearlyZeroSquared) // always, since the pitch stays below 90 degrees
    {
        const glm::vec3 forward = glm::normalize(ground);
        const glm::vec3 right   = glm::cross(forward, WorldUp);
        move += forward * forwardAmount + right * rightAmount;
    }

    const bool isMoved = glm::dot(move, move) >= NearlyZeroSquared;
    if (!isMoved && !isTurned)
    {
        return; // untouched, so the camera keeps its matrices
    }

    const glm::vec3 offset =
        isMoved ? glm::normalize(move) * MoveSpeed * seconds : glm::vec3(0.0f); // diagonals no faster
    camera.SetPosition(position + offset);
    camera.SetTarget(position + offset + view); // the target moves along, so the camera keeps looking the same way
}

bool Wotah::CameraController::SetHeld(KeyCode key, bool isHeld) noexcept
{
    switch (key)
    {
        case KeyCode::W:
            m_held.Forward = isHeld;
            return true;
        case KeyCode::S:
            m_held.Backward = isHeld;
            return true;
        case KeyCode::A:
            m_held.Left = isHeld;
            return true;
        case KeyCode::D:
            m_held.Right = isHeld;
            return true;
        case KeyCode::Space:
            m_held.Up = isHeld;
            return true;
        case KeyCode::LeftShift:
            m_held.LeftShift = isHeld;
            return true;
        case KeyCode::RightShift:
            m_held.RightShift = isHeld;
            return true;
        default:
            return false;
    }
}

void Wotah::CameraController::ReleaseAll() noexcept
{
    m_held        = HeldKeys{};
    m_isTurning   = false;
    m_pendingTurn = glm::vec2(0.0f);
}
