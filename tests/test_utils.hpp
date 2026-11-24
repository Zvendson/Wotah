/**
 * @file test_utils.hpp
 * Helpers several test files use: comparing floats and vectors with a tolerance, building input events, and a hidden
 * window with a device for the GPU tests.
 */
#pragma once

#include "input/input_event.hpp"
#include "platform/window.hpp"
#include "render/device.hpp"

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

    /** The hidden test window's size. */
    inline constexpr WindowSize HiddenWindowSize{640, 480};

    /**
     * A hidden window and a device on it, where most GPU tests start. The window is never shown, so tests never put
     * anything on screen.
     */
    struct HiddenGpu
    {
        Wotah::Window Window;
        Wotah::Device Device; // declared after the window, so it is destroyed first

        [[nodiscard]] bool Create()
        {
            return Window.Create(HiddenWindowSize, "wotah tests (hidden)") && Device.Create(Window, "wotah tests");
        }
    };
} // namespace Wotah::Testing
