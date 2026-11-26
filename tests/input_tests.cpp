/**
 * @file input_tests.cpp
 * CameraController, SceneShortcuts and InputDispatcher with made-up events: movement per key, turning, zooming, the F
 * key, the R key, focus loss, and a pretend UI in front of the camera.
 */
#include "input/camera_controller.hpp"
#include "input/input_dispatcher.hpp"
#include "input/input_event.hpp"
#include "input/input_handler.hpp"
#include "input/scene_shortcuts.hpp"
#include "render/camera.hpp"
#include "test_utils.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

using Wotah::Testing::IsNear;
using Wotah::Testing::MakeButtonEvent;
using Wotah::Testing::MakeFocusLostEvent;
using Wotah::Testing::MakeKeyEvent;
using Wotah::Testing::MakeMoveEvent;
using Wotah::Testing::MakeScrollEvent;

namespace
{
    /** The controller's flying speed (camera_controller.cpp), in units per second. */
    constexpr float MoveSpeed = 3.0f;

    /** A camera at the origin looking along -z at a target 1 unit away; defaults: near 0.1, far 100. */
    Wotah::Camera MakeCamera()
    {
        Wotah::Camera camera;
        camera.SetPosition({0.0f, 0.0f, 0.0f});
        camera.SetTarget({0.0f, 0.0f, -1.0f});
        return camera;
    }

    /** The distance from the camera to its target. */
    float GetDistance(const Wotah::Camera& camera)
    {
        return glm::length(camera.GetTarget() - camera.GetPosition());
    }

    /** A pretend UI: uses left clicks, and every key press and typed character while a text field has the focus. */
    class FakeUi final : public Wotah::InputHandler
    {
    public:
        bool                           IsTyping = false;
        std::vector<Wotah::InputEvent> Seen;

        bool OnInput(const Wotah::InputEvent& event) override
        {
            Seen.push_back(event);
            if (event.Type == Wotah::InputEventType::MouseButtonPressed && event.Button == Wotah::MouseButton::Left)
            {
                return true;
            }
            return IsTyping && (event.Type == Wotah::InputEventType::KeyPressed ||
                                event.Type == Wotah::InputEventType::TextEntered);
        }
    };
} // namespace

TEST_SUITE("input")
{
    TEST_CASE("each movement key moves the camera 3 units per second the right way")
    {
        struct Case
        {
            Wotah::KeyCode Key = Wotah::KeyCode::Unknown;
            glm::vec3      Expected{0.0f};
        };

        const Case cases[] = {
            {Wotah::KeyCode::W, {0.0f, 0.0f, -MoveSpeed}},
            {Wotah::KeyCode::S, {0.0f, 0.0f, MoveSpeed}},
            {Wotah::KeyCode::A, {-MoveSpeed, 0.0f, 0.0f}},
            {Wotah::KeyCode::D, {MoveSpeed, 0.0f, 0.0f}},
            {Wotah::KeyCode::Space, {0.0f, MoveSpeed, 0.0f}},
            {Wotah::KeyCode::LeftShift, {0.0f, -MoveSpeed, 0.0f}},
            {Wotah::KeyCode::RightShift, {0.0f, -MoveSpeed, 0.0f}},
        };
        for (const Case& c : cases)
        {
            CAPTURE(static_cast<int>(c.Key));
            Wotah::CameraController controller;
            Wotah::Camera           camera = MakeCamera();
            CHECK(controller.OnInput(MakeKeyEvent(c.Key, true)));
            controller.Update(camera, 1.0f);
            CHECK(IsNear(camera.GetPosition(), c.Expected));
            CHECK(IsNear(camera.GetTarget() - camera.GetPosition(), {0.0f, 0.0f, -1.0f})); // still looks the same way
        }
    }

    TEST_CASE("combined keys: diagonals are not faster, opposites cancel, let go stops")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::D, true));
        controller.Update(camera, 1.0f);
        CHECK(IsNear(glm::length(camera.GetPosition()), MoveSpeed));

        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, false));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::D, false));
        const glm::vec3 still = camera.GetPosition();
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), still));

        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::S, true));
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), still));
    }

    TEST_CASE("both Shift keys count separately, other keys are not used")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::LeftShift, true));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::RightShift, true));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::LeftShift, false));
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, -MoveSpeed, 0.0f}));
        CHECK_FALSE(controller.OnInput(MakeKeyEvent(Wotah::KeyCode::E, true)));
    }

    TEST_CASE("W moves along the ground even when looking down")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera;
        camera.SetPosition({0.0f, 0.0f, 0.0f});
        camera.SetTarget({0.0f, -1.0f, -1.0f});
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true));
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, -MoveSpeed}));
    }

    TEST_CASE("the right mouse button turns the camera")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();

        CHECK_FALSE(controller.OnInput(MakeMoveEvent(100.0f, 0.0f))); // not turning yet
        controller.Update(camera, 0.016f);
        CHECK(IsNear(camera.GetTarget(), {0.0f, 0.0f, -1.0f}));

        CHECK(controller.OnInput(MakeButtonEvent(Wotah::MouseButton::Right, true)));
        CHECK(controller.IsTurning());
        CHECK(controller.OnInput(MakeMoveEvent(100.0f, 0.0f)));
        controller.Update(camera, 0.016f);
        const float turned = glm::radians(15.0f); // 100 pixels at 0.15 degrees each
        CHECK(IsNear(camera.GetTarget() - camera.GetPosition(), {std::sin(turned), 0.0f, -std::cos(turned)}));

        (void)controller.OnInput(MakeMoveEvent(0.0f, -100000.0f)); // far up
        controller.Update(camera, 0.016f);
        const glm::vec3 direction = glm::normalize(camera.GetTarget() - camera.GetPosition());
        CHECK(IsNear(std::asin(direction.y), glm::radians(89.0f)));

        CHECK(controller.OnInput(MakeButtonEvent(Wotah::MouseButton::Right, false)));
        CHECK_FALSE(controller.IsTurning());
        CHECK_FALSE(controller.OnInput(MakeButtonEvent(Wotah::MouseButton::Left, true)));
    }

    TEST_CASE("the wheel zooms by 10% of the distance, within the camera's limits")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera(); // near 0.1, far 100: zoom from 0.2 to 50

        CHECK(controller.OnInput(MakeScrollEvent(0.0f, 1.0f)));
        controller.Update(camera, 0.016f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, -0.1f}));
        CHECK(IsNear(camera.GetTarget(), {0.0f, 0.0f, -1.0f}));

        (void)controller.OnInput(MakeScrollEvent(0.0f, -1.0f));
        controller.Update(camera, 0.016f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, 0.0f}));

        (void)controller.OnInput(MakeScrollEvent(0.0f, 0.5f)); // touchpad halves add up
        (void)controller.OnInput(MakeScrollEvent(0.0f, 0.5f));
        controller.Update(camera, 0.016f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, -0.1f}));

        (void)controller.OnInput(MakeScrollEvent(0.0f, 1000.0f));
        controller.Update(camera, 0.016f);
        CHECK(IsNear(GetDistance(camera), 0.2f));

        (void)controller.OnInput(MakeScrollEvent(0.0f, -1000.0f));
        controller.Update(camera, 0.016f);
        CHECK(IsNear(GetDistance(camera), 50.0f));
        CHECK(IsNear(glm::normalize(camera.GetTarget() - camera.GetPosition()), {0.0f, 0.0f, -1.0f}));

        CHECK_FALSE(controller.OnInput(MakeScrollEvent(1.0f, 0.0f))); // sideways
    }

    TEST_CASE("zoom and movement in one frame")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        (void)controller.OnInput(MakeScrollEvent(0.0f, 1.0f));
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true));
        controller.Update(camera, 1.0f);
        CHECK(IsNear(GetDistance(camera), 0.9f));
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, -0.1f - MoveSpeed}));
    }

    TEST_CASE("F asks for the focus view once and drops what was not applied yet")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        CHECK_FALSE(controller.TakeFocusRequest());

        Wotah::InputEvent f = MakeKeyEvent(Wotah::KeyCode::F, true);
        CHECK(controller.OnInput(f));
        CHECK(controller.TakeFocusRequest());
        CHECK_FALSE(controller.TakeFocusRequest());

        f.IsRepeat = true; // holding F
        (void)controller.OnInput(f);
        CHECK_FALSE(controller.TakeFocusRequest());

        (void)controller.OnInput(MakeButtonEvent(Wotah::MouseButton::Right, true));
        (void)controller.OnInput(MakeMoveEvent(100.0f, 0.0f));
        (void)controller.OnInput(MakeScrollEvent(0.0f, 3.0f));
        f.IsRepeat = false;
        (void)controller.OnInput(f);
        CHECK(controller.TakeFocusRequest());
        controller.Update(camera, 0.016f);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, 0.0f}));
        CHECK(IsNear(camera.GetTarget(), {0.0f, 0.0f, -1.0f}));
    }

    TEST_CASE("losing the focus lets go of everything")
    {
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        (void)controller.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true));
        (void)controller.OnInput(MakeButtonEvent(Wotah::MouseButton::Right, true));
        (void)controller.OnInput(MakeMoveEvent(50.0f, 0.0f));
        (void)controller.OnInput(MakeScrollEvent(0.0f, 2.0f));
        (void)controller.OnInput(MakeFocusLostEvent());
        controller.Update(camera, 1.0f);
        CHECK_FALSE(controller.IsTurning());
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, 0.0f}));
        CHECK(IsNear(camera.GetTarget(), {0.0f, 0.0f, -1.0f}));
    }

    TEST_CASE("the dispatcher asks in order, and releases reach everyone")
    {
        Wotah::InputDispatcher  dispatcher;
        FakeUi                  ui;
        Wotah::CameraController controller;
        Wotah::Camera           camera = MakeCamera();
        dispatcher.AddHandler(ui);
        dispatcher.AddHandler(controller);
        dispatcher.AddHandler(ui); // twice: ignored

        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::W, true));
        controller.Update(camera, 1.0f);
        CHECK(ui.Seen.size() == 1);
        CHECK(IsNear(camera.GetPosition(), {0.0f, 0.0f, -MoveSpeed})); // unused by the UI, so the camera got it

        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::W, false));
        ui.IsTyping                  = true;
        const glm::vec3 beforeTyping = camera.GetPosition();
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::W, true)); // typing a "w"
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), beforeTyping));

        ui.IsTyping = false;
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::D, true)); // the camera holds D
        ui.IsTyping = true;
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::D, false)); // must still reach the camera
        const glm::vec3 beforeRelease = camera.GetPosition();
        controller.Update(camera, 1.0f);
        CHECK(IsNear(camera.GetPosition(), beforeRelease));

        const std::size_t seenBeforeClick = ui.Seen.size();
        dispatcher.Dispatch(MakeButtonEvent(Wotah::MouseButton::Left, true));
        CHECK(ui.Seen.size() == seenBeforeClick + 1);
        dispatcher.Dispatch(MakeFocusLostEvent());
        CHECK(ui.Seen.back().Type == Wotah::InputEventType::FocusLost);

        dispatcher.RemoveHandler(ui);
        const std::size_t seenAfterRemove = ui.Seen.size();
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::W, true));
        controller.Update(camera, 1.0f);
        CHECK(ui.Seen.size() == seenAfterRemove);
        CHECK_FALSE(IsNear(camera.GetPosition(), beforeRelease));
    }
    TEST_CASE(
        "R asks once to reset the scene, also when held; behind a UI text field that has the keyboard it never does")
    {
        Wotah::SceneShortcuts shortcuts;
        CHECK_FALSE(shortcuts.TakeResetRequest());
        CHECK(shortcuts.OnInput(MakeKeyEvent(Wotah::KeyCode::R, true)));
        CHECK(shortcuts.TakeResetRequest());
        CHECK_FALSE(shortcuts.TakeResetRequest()); // answered once

        // Held, the system repeats R, but that is no new press.
        Wotah::InputEvent repeated = MakeKeyEvent(Wotah::KeyCode::R, true);
        repeated.IsRepeat          = true;
        CHECK(shortcuts.OnInput(repeated));
        CHECK_FALSE(shortcuts.TakeResetRequest());

        // Other keys and letting go of R go on to the handlers after it.
        CHECK_FALSE(shortcuts.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true)));
        CHECK_FALSE(shortcuts.OnInput(MakeKeyEvent(Wotah::KeyCode::R, false)));
        CHECK_FALSE(shortcuts.TakeResetRequest());

        // In the program's order, UI, camera, scene keys: the camera lets R through, a typing UI keeps it.
        FakeUi                  ui;
        Wotah::CameraController camera;
        Wotah::InputDispatcher  dispatcher;
        dispatcher.AddHandler(ui);
        dispatcher.AddHandler(camera);
        dispatcher.AddHandler(shortcuts);
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::R, true));
        CHECK(shortcuts.TakeResetRequest());
        ui.IsTyping = true;
        dispatcher.Dispatch(MakeKeyEvent(Wotah::KeyCode::R, true));
        CHECK_FALSE(shortcuts.TakeResetRequest());
    }
}
