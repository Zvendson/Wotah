/**
 * @file scene_shortcuts.cpp
 * The scene's keys.
 */
#include "input/scene_shortcuts.hpp"

#include "input/input_event.hpp"

bool Wotah::SceneShortcuts::OnInput(const InputEvent& event)
{
    if (event.Type != InputEventType::KeyPressed || event.Key != KeyCode::R)
    {
        return false;
    }
    m_isResetRequested = m_isResetRequested || !event.IsRepeat; // holding R asks once
    return true;
}

bool Wotah::SceneShortcuts::TakeResetRequest() noexcept
{
    const bool isRequested = m_isResetRequested;
    m_isResetRequested     = false;
    return isRequested;
}
