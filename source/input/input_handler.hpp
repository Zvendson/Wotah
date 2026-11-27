/**
 * @file input_handler.hpp
 * The contract of everything that reacts to input: the camera controller now, the UI later.
 *
 *     class MyHandler final : public Wotah::InputHandler
 *     {
 *     public:
 *         bool OnInput(const Wotah::InputEvent& event) override
 *         {
 *             return event.Type == Wotah::InputEventType::KeyPressed && event.Key == Wotah::KeyCode::Escape;
 *         }
 *     };
 */
#pragma once

namespace Wotah
{
    struct InputEvent;

    /** Something that reacts to input events; the InputDispatcher asks its handlers in order. */
    class InputHandler
    {
    public:
        virtual ~InputHandler() = default;

        /**
         * Reacts to an event. Returns true when it used the event, so handlers after it do not get it; releases and
         * focus loss reach every handler anyway (InputEvent::IsRelease), so a handler never misses a key being let go.
         */
        virtual bool OnInput(const InputEvent& event) = 0;

    protected:
        InputHandler()                               = default;
        InputHandler(const InputHandler&)            = default;
        InputHandler& operator=(const InputHandler&) = default;
    };
} // namespace Wotah
