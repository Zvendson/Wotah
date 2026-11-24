/**
 * @file input_event.hpp
 * What the user did: keys, mouse buttons, mouse movement, scrolling, typed text, and the window losing focus.
 * The window collects these every frame (Window::GetInputEvents); the InputDispatcher hands them to the handlers.
 */
#pragma once

#include <glm/vec2.hpp>

#include <cstdint>

namespace Wotah
{
    /**
     * A keyboard key. The values are GLFW 3.4's key codes (glfw3.h, "Keyboard keys"), so every key arrives, named here
     * or not; window.cpp checks at compile time that the named ones match.
     */
    enum class KeyCode : int32_t
    {
        Unknown      = -1,
        Space        = 32,
        Digit0       = 48,
        Digit1       = 49,
        Digit2       = 50,
        Digit3       = 51,
        Digit4       = 52,
        Digit5       = 53,
        Digit6       = 54,
        Digit7       = 55,
        Digit8       = 56,
        Digit9       = 57,
        A            = 65,
        B            = 66,
        C            = 67,
        D            = 68,
        E            = 69,
        F            = 70,
        G            = 71,
        H            = 72,
        I            = 73,
        J            = 74,
        K            = 75,
        L            = 76,
        M            = 77,
        N            = 78,
        O            = 79,
        P            = 80,
        Q            = 81,
        R            = 82,
        S            = 83,
        T            = 84,
        U            = 85,
        V            = 86,
        W            = 87,
        X            = 88,
        Y            = 89,
        Z            = 90,
        Escape       = 256,
        Enter        = 257,
        Tab          = 258,
        Backspace    = 259,
        Delete       = 261,
        Right        = 262,
        Left         = 263,
        Down         = 264,
        Up           = 265,
        LeftShift    = 340,
        LeftControl  = 341,
        LeftAlt      = 342,
        RightShift   = 344,
        RightControl = 345,
        RightAlt     = 346,
    };

    /** A mouse button; the values are GLFW 3.4's button numbers (glfw3.h, "Mouse buttons"). */
    enum class MouseButton : int32_t
    {
        Left   = 0,
        Right  = 1,
        Middle = 2,
    };

    /** What kind of thing happened. */
    enum class InputEventType
    {
        /** A key went down, or is held long enough that the system repeats it (IsRepeat). */
        KeyPressed,
        /** A key was let go. */
        KeyReleased,
        /** A mouse button went down. */
        MouseButtonPressed,
        /** A mouse button was let go. */
        MouseButtonReleased,
        /** The mouse moved (Delta) to CursorPosition. */
        MouseMoved,
        /** The wheel or touchpad scrolled by Delta. */
        MouseScrolled,
        /** A character was typed (Character), for text fields; also arrives as KeyPressed. */
        TextEntered,
        /** The window lost the keyboard focus: every key and button counts as let go. */
        FocusLost,
    };

    /** One thing the user did. Only the fields its Type names are filled; the others keep their defaults. */
    struct InputEvent
    {
        /** What happened. */
        InputEventType Type = InputEventType::FocusLost;

        /** KeyPressed, KeyReleased: which key. */
        KeyCode Key = KeyCode::Unknown;

        /** KeyPressed: true when the system repeats a held key, false for the first press. */
        bool IsRepeat = false;

        /** MouseButtonPressed, MouseButtonReleased: which button. */
        MouseButton Button = MouseButton::Left;

        /** Mouse events: the cursor in window pixels from the top-left corner. */
        glm::vec2 CursorPosition{0.0f};

        /** MouseMoved: the movement since the last one, in pixels. MouseScrolled: the scroll steps (y = wheel). */
        glm::vec2 Delta{0.0f};

        /** TextEntered: the typed character as a Unicode code point. */
        char32_t Character = 0;

        /** Whether the event ends something held (a key or button let go, focus lost); those reach every handler. */
        [[nodiscard]] bool IsRelease() const noexcept
        {
            return Type == InputEventType::KeyReleased || Type == InputEventType::MouseButtonReleased ||
                   Type == InputEventType::FocusLost;
        }
    };
} // namespace Wotah
