/**
 * @file test_utils.hpp
 * Helpers several test files use: comparing floats and vectors with a tolerance, and building input events.
 */
#pragma once

#include "input/input_event.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>

namespace Wotah::Testing
{
    /** How far apart two floats may be and still count as equal: well below anything a test measures. */
    inline constexpr float Tolerance = 1.0e-4f;

    /** Whether two floats are equal within Tolerance. */
    inline bool IsNear(float a, float b)
    {
        return std::abs(a - b) < Tolerance;
    }

    /** Whether two vectors are equal within Tolerance. */
    inline bool IsNear(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(a - b) < Tolerance;
    }

    /** A key going down or being let go. */
    inline InputEvent MakeKeyEvent(KeyCode key, bool isPressed)
    {
        InputEvent event;
        event.Type = isPressed ? InputEventType::KeyPressed : InputEventType::KeyReleased;
        event.Key  = key;
        return event;
    }

    /** A mouse button going down or being let go. */
    inline InputEvent MakeButtonEvent(MouseButton button, bool isPressed)
    {
        InputEvent event;
        event.Type   = isPressed ? InputEventType::MouseButtonPressed : InputEventType::MouseButtonReleased;
        event.Button = button;
        return event;
    }

    /** The mouse moving by (x, y) pixels. */
    inline InputEvent MakeMoveEvent(float x, float y)
    {
        InputEvent event;
        event.Type  = InputEventType::MouseMoved;
        event.Delta = {x, y};
        return event;
    }

    /** The wheel scrolling by (x, y) steps; y is the normal wheel. */
    inline InputEvent MakeScrollEvent(float x, float y)
    {
        InputEvent event;
        event.Type  = InputEventType::MouseScrolled;
        event.Delta = {x, y};
        return event;
    }

    /** The window losing the focus. */
    inline InputEvent MakeFocusLostEvent()
    {
        InputEvent event;
        event.Type = InputEventType::FocusLost;
        return event;
    }
} // namespace Wotah::Testing
