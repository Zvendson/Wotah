/**
 * @file scene_shortcuts.cpp
 * The scene's keys.
 */
#include "input/scene_shortcuts.hpp"

#include "input/input_event.hpp"

namespace
{
    /** Notes a press in request, unless it is the system repeating a held key: holding a key asks once. */
    bool NotePress(const Wotah::InputEvent& event, bool request) noexcept
    {
        return request || !event.IsRepeat;
    }

    /** Hands back whether request was set, and clears it. */
    bool Take(bool& request) noexcept
    {
        const bool isRequested = request;
        request                = false;
        return isRequested;
    }
} // namespace

bool Wotah::SceneShortcuts::OnInput(const InputEvent& event)
{
    if (event.Type != InputEventType::KeyPressed)
    {
        return false;
    }
    switch (event.Key)
    {
        case KeyCode::R:
            m_isResetRequested = NotePress(event, m_isResetRequested);
            return true;
        case KeyCode::Tab:
            m_isSettingsRequested = NotePress(event, m_isSettingsRequested);
            return true;
        default:
            return false;
    }
}

bool Wotah::SceneShortcuts::TakeResetRequest() noexcept
{
    return Take(m_isResetRequested);
}

bool Wotah::SceneShortcuts::TakeSettingsRequest() noexcept
{
    return Take(m_isSettingsRequested);
}
