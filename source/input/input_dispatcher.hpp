/**
 * @file input_dispatcher.hpp
 * Hands every input event to the handlers in order until one uses it. Add the UI first and the camera after it, so a
 * click on a button or typing into a field never also moves the camera.
 *
 *     Wotah::InputDispatcher dispatcher;
 *     dispatcher.AddHandler(ui);               // asked first
 *     dispatcher.AddHandler(cameraController); // gets what the UI did not use
 *     window.TakeInputEvents(events);
 *     for (const Wotah::InputEvent& event : events)
 *     {
 *         dispatcher.Dispatch(event);
 *     }
 */
#pragma once

#include <vector>

namespace Wotah
{
    class InputHandler;
    struct InputEvent;

    /** An ordered list of input handlers; it does not own them. */
    class InputDispatcher final
    {
    public:
        /**
         * Adds a handler after the ones already there; adding the same one twice does nothing. It must outlive the
         * dispatcher or be removed first.
         */
        void AddHandler(InputHandler& handler);

        /** Removes a handler; unknown ones are ignored. */
        void RemoveHandler(InputHandler& handler);

        /**
         * Asks the handlers in order until one uses the event. Releases and focus loss go to every handler, so a key
         * pressed while one handler had the event is still let go everywhere.
         */
        void Dispatch(const InputEvent& event);

    private:
        /** The handlers in the order they are asked; not owned. */
        std::vector<InputHandler*> m_handlers;
    };
} // namespace Wotah
