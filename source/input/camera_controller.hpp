/**
 * @file camera_controller.hpp
 * Flies a camera with the keyboard and mouse: W A S D move along the ground, Space up, Shift down, and holding the
 * right mouse button turns the camera.
 *
 *     Wotah::CameraController controller;
 *     dispatcher.AddHandler(controller);
 *     // every frame, after the input events were dispatched:
 *     window.SetCursorCaptured(controller.IsTurning());
 *     controller.Update(camera, frameSeconds);
 */
#pragma once

#include "input/input_handler.hpp"

#include <glm/vec2.hpp>

#include <cstdint>

namespace Wotah
{
    class Camera;
    enum class KeyCode : int32_t;

    /**
     * Turns input events into camera movement. It only remembers what is held; Update moves the camera, so the speed
     * is the same at any frame rate. It assumes y is up in the world.
     */
    class CameraController final : public InputHandler
    {
    public:
        /** Uses the movement keys, the right mouse button and, while turning, mouse movement. */
        bool OnInput(const InputEvent& event) override;

        /** Moves and turns the camera by what was held and moved since the last call; seconds is the frame time. */
        void Update(Camera& camera, float seconds);

        /** Whether the right mouse button is held: the window should hide and lock the cursor meanwhile. */
        [[nodiscard]] bool IsTurning() const noexcept { return m_isTurning; }

    private:
        /** The movement keys held right now; both Shift keys move down, so each is tracked. */
        struct HeldKeys
        {
            bool Forward    = false;
            bool Backward   = false;
            bool Left       = false;
            bool Right      = false;
            bool Up         = false;
            bool LeftShift  = false;
            bool RightShift = false;
        };

        /** Marks a movement key as held or let go; false for keys that do not move the camera. */
        bool SetHeld(KeyCode key, bool isHeld) noexcept;

        /** Lets go of every key and button, e.g. when the window loses the focus. */
        void ReleaseAll() noexcept;

        HeldKeys m_held;

        /** Whether the right mouse button is held. */
        bool m_isTurning = false;

        /** The mouse movement in pixels while turning, not yet applied to the camera. */
        glm::vec2 m_pendingTurn{0.0f};
    };
} // namespace Wotah
