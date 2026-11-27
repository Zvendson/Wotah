/**
 * @file input_dispatcher.cpp
 * The order in which input handlers get events.
 */
#include "input/input_dispatcher.hpp"

#include "input/input_event.hpp"
#include "input/input_handler.hpp"

#include <algorithm>

void Wotah::InputDispatcher::AddHandler(InputHandler& handler)
{
    if (std::ranges::find(m_handlers, &handler) == m_handlers.end())
    {
        m_handlers.push_back(&handler);
    }
}

void Wotah::InputDispatcher::RemoveHandler(InputHandler& handler)
{
    std::erase(m_handlers, &handler);
}

void Wotah::InputDispatcher::Dispatch(const InputEvent& event)
{
    // A release goes to everyone: the handler that saw the press may sit behind the one that would use the release.
    const bool reachesEveryone = event.IsRelease();
    for (InputHandler* handler : m_handlers)
    {
        if (handler->OnInput(event) && !reachesEveryone)
        {
            return;
        }
    }
}
