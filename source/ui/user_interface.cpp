/**
 * @file user_interface.cpp
 * ImGui's context, and the bridge from the window's input events to ImGui's input queue (Dear ImGui 1.93.0, imgui.h:
 * ImGuiIO::AddKeyEvent, AddMousePosEvent, AddMouseButtonEvent, AddMouseWheelEvent, AddInputCharacter, AddFocusEvent).
 */
#include "ui/user_interface.hpp"

#include "core/logger.hpp"
#include "input/input_event.hpp"
#include "platform/window.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <algorithm>

namespace
{
    /**
     * The shortest frame ImGui is told about: it refuses a frame time of 0 (imgui.cpp, ErrorCheckNewFrameSanityChecks),
     * which a coarse clock can measure between two quick frames. One microsecond.
     */
    constexpr float MinFrameSeconds = 1.0e-6f;

    /** The scale between layout units and pixels on screens the system does not scale. */
    constexpr ImVec2 NoScale{1.0f, 1.0f};

    // Letters and digits come in the same order in both lists, so they translate by their distance from the first.
    static_assert(ImGuiKey_Z - ImGuiKey_A == static_cast<int>(Wotah::KeyCode::Z) - static_cast<int>(Wotah::KeyCode::A));
    static_assert(ImGuiKey_9 - ImGuiKey_0 ==
                  static_cast<int>(Wotah::KeyCode::Digit9) - static_cast<int>(Wotah::KeyCode::Digit0));

    // The named mouse buttons have ImGui's numbers too (GLFW and ImGui both count left, right, middle from 0).
    static_assert(static_cast<int>(Wotah::MouseButton::Left) == ImGuiMouseButton_Left);
    static_assert(static_cast<int>(Wotah::MouseButton::Right) == ImGuiMouseButton_Right);
    static_assert(static_cast<int>(Wotah::MouseButton::Middle) == ImGuiMouseButton_Middle);

    /** How far key comes after first. */
    int GetDistance(Wotah::KeyCode key, Wotah::KeyCode first)
    {
        return static_cast<int>(key) - static_cast<int>(first);
    }

    /** ImGui's name for a key; ImGuiKey_None for keys the UI has no use for. */
    ImGuiKey ToImGuiKey(Wotah::KeyCode key)
    {
        using Wotah::KeyCode;
        if (key >= KeyCode::A && key <= KeyCode::Z)
        {
            return static_cast<ImGuiKey>(ImGuiKey_A + GetDistance(key, KeyCode::A));
        }
        if (key >= KeyCode::Digit0 && key <= KeyCode::Digit9)
        {
            return static_cast<ImGuiKey>(ImGuiKey_0 + GetDistance(key, KeyCode::Digit0));
        }
        switch (key)
        {
            case KeyCode::Space:
                return ImGuiKey_Space;
            case KeyCode::Escape:
                return ImGuiKey_Escape;
            case KeyCode::Enter:
                return ImGuiKey_Enter;
            case KeyCode::KeypadEnter:
                return ImGuiKey_KeypadEnter;
            case KeyCode::Tab:
                return ImGuiKey_Tab;
            case KeyCode::Backspace:
                return ImGuiKey_Backspace;
            case KeyCode::Insert:
                return ImGuiKey_Insert;
            case KeyCode::Delete:
                return ImGuiKey_Delete;
            case KeyCode::Right:
                return ImGuiKey_RightArrow;
            case KeyCode::Left:
                return ImGuiKey_LeftArrow;
            case KeyCode::Down:
                return ImGuiKey_DownArrow;
            case KeyCode::Up:
                return ImGuiKey_UpArrow;
            case KeyCode::PageUp:
                return ImGuiKey_PageUp;
            case KeyCode::PageDown:
                return ImGuiKey_PageDown;
            case KeyCode::Home:
                return ImGuiKey_Home;
            case KeyCode::End:
                return ImGuiKey_End;
            case KeyCode::LeftShift:
                return ImGuiKey_LeftShift;
            case KeyCode::LeftControl:
                return ImGuiKey_LeftCtrl;
            case KeyCode::LeftAlt:
                return ImGuiKey_LeftAlt;
            case KeyCode::LeftSuper:
                return ImGuiKey_LeftSuper;
            case KeyCode::RightShift:
                return ImGuiKey_RightShift;
            case KeyCode::RightControl:
                return ImGuiKey_RightCtrl;
            case KeyCode::RightAlt:
                return ImGuiKey_RightAlt;
            case KeyCode::RightSuper:
                return ImGuiKey_RightSuper;
            default:
                return ImGuiKey_None;
        }
    }

    /** A size as ImGui's vector. */
    ImVec2 ToVector(const Wotah::WindowSize& size)
    {
        return {static_cast<float>(size.Width), static_cast<float>(size.Height)};
    }
} // namespace

void Wotah::UserInterface::ContextDeleter::operator()(ImGuiContext* context) const noexcept
{
    ImGui::DestroyContext(context);
}

Wotah::UserInterface::~UserInterface()
{
    Destroy();
}

bool Wotah::UserInterface::Create()
{
    Destroy();
    if (ImGui::GetCurrentContext() != nullptr)
    {
        Logger::Error("Dear ImGui is already running; there can only be one UI at a time");
        return false;
    }
    m_context.reset(ImGui::CreateContext());

    // Nothing is written beside the executable: no window places (imgui.ini), no log file.
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // ImGui's own scalable font (ProggyForever), sharp at the theme's size; it is built into ImGui, so nothing is read
    // from disk.
    if (io.Fonts->AddFontDefaultVector() == nullptr)
    {
        Logger::Error("Could not load the UI's font");
        Destroy();
        return false;
    }
    ApplyTheme(ImGui::GetStyle());
    return true;
}

void Wotah::UserInterface::Destroy()
{
    if (!m_context)
    {
        return;
    }
    // ImGui refuses to stop while a renderer is attached to it (imgui.cpp, Shutdown): its GPU objects would leak.
    if (ImGui::GetIO().BackendRendererUserData != nullptr)
    {
        Logger::Error("The UI keeps running: its renderer must be destroyed first");
        return;
    }
    m_context.reset();
    m_modifiers = {};
}

bool Wotah::UserInterface::OnInput(const InputEvent& event)
{
    if (!m_context)
    {
        return false;
    }

    // shortcut: the window reports no cursor leaving it, so a widget under the cursor stays highlighted after the
    // cursor left the window; add such an event to the window when that matters.
    ImGuiIO& io = ImGui::GetIO();
    switch (event.Type)
    {
        case InputEventType::MouseMoved:
            io.AddMousePosEvent(event.CursorPosition.x, event.CursorPosition.y);
            return io.WantCaptureMouse;
        case InputEventType::MouseButtonPressed:
        case InputEventType::MouseButtonReleased:
        {
            // ImGui knows 5 buttons; the others it ignores. The press may come without a move first (after a focus
            // change), so the cursor's place comes with it.
            const int button = static_cast<int>(event.Button);
            io.AddMousePosEvent(event.CursorPosition.x, event.CursorPosition.y);
            if (button >= 0 && button < ImGuiMouseButton_COUNT)
            {
                io.AddMouseButtonEvent(button, event.Type == InputEventType::MouseButtonPressed);
            }
            return io.WantCaptureMouse;
        }
        case InputEventType::MouseScrolled:
            io.AddMouseWheelEvent(event.Delta.x, event.Delta.y);
            return io.WantCaptureMouse;
        case InputEventType::KeyPressed:
        case InputEventType::KeyReleased:
            SendKey(event);
            return io.WantCaptureKeyboard;
        case InputEventType::TextEntered:
            io.AddInputCharacter(static_cast<unsigned int>(event.Character));
            return io.WantCaptureKeyboard;
        case InputEventType::FocusLost:
            // ImGui lets go of every key and button itself; the modifiers here are let go too.
            io.AddFocusEvent(false);
            m_modifiers = {};
            return false; // reaches every handler anyway
    }
    return false;
}

void Wotah::UserInterface::BeginFrame(const WindowSize& windowSize, const WindowSize& pictureSize, float seconds)
{
    if (!m_context)
    {
        return;
    }

    // ImGui lays the windows out in the cursor's units (DisplaySize) and draws them at DisplaySize times
    // DisplayFramebufferScale pixels, which must be the picture's size. A window without area has no ratio to the
    // picture, so then the picture's size serves as both.
    ImGuiIO&   io              = ImGui::GetIO();
    const bool hasWindowArea   = windowSize.Width > 0 && windowSize.Height > 0;
    io.DisplaySize             = ToVector(hasWindowArea ? windowSize : pictureSize);
    io.DisplayFramebufferScale = hasWindowArea ? ImVec2(static_cast<float>(pictureSize.Width) / io.DisplaySize.x,
                                                        static_cast<float>(pictureSize.Height) / io.DisplaySize.y)
                                               : NoScale;
    io.DeltaTime               = std::max(seconds, MinFrameSeconds);
    ImGui::NewFrame();
}

void Wotah::UserInterface::EndFrame()
{
    if (!m_context)
    {
        return;
    }
    ImGui::Render();
}

void Wotah::UserInterface::SendKey(const InputEvent& event)
{
    // ImGui repeats held keys itself (ImGuiIO::KeyRepeatDelay, KeyRepeatRate), so the system's repeats are left out.
    if (event.IsRepeat)
    {
        return;
    }
    const bool isDown = event.Type == InputEventType::KeyPressed;
    SetModifierHeld(event.Key, isDown);

    // The modifiers go first, so a shortcut such as Ctrl+C finds Ctrl already down. ImGui drops repeated states.
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, m_modifiers.LeftControl || m_modifiers.RightControl);
    io.AddKeyEvent(ImGuiMod_Shift, m_modifiers.LeftShift || m_modifiers.RightShift);
    io.AddKeyEvent(ImGuiMod_Alt, m_modifiers.LeftAlt || m_modifiers.RightAlt);
    io.AddKeyEvent(ImGuiMod_Super, m_modifiers.LeftSuper || m_modifiers.RightSuper);

    const ImGuiKey key = ToImGuiKey(event.Key);
    if (key != ImGuiKey_None)
    {
        io.AddKeyEvent(key, isDown);
    }
}

void Wotah::UserInterface::SetModifierHeld(KeyCode key, bool isHeld) noexcept
{
    switch (key)
    {
        case KeyCode::LeftControl:
            m_modifiers.LeftControl = isHeld;
            break;
        case KeyCode::RightControl:
            m_modifiers.RightControl = isHeld;
            break;
        case KeyCode::LeftShift:
            m_modifiers.LeftShift = isHeld;
            break;
        case KeyCode::RightShift:
            m_modifiers.RightShift = isHeld;
            break;
        case KeyCode::LeftAlt:
            m_modifiers.LeftAlt = isHeld;
            break;
        case KeyCode::RightAlt:
            m_modifiers.RightAlt = isHeld;
            break;
        case KeyCode::LeftSuper:
            m_modifiers.LeftSuper = isHeld;
            break;
        case KeyCode::RightSuper:
            m_modifiers.RightSuper = isHeld;
            break;
        default:
            break; // not a modifier
    }
}
