/**
 * @file window.cpp
 * The window on top of GLFW 3.4.
 */
#include "platform/window.hpp"

#include "core/logger.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>

// KeyCode and MouseButton take GLFW's numbers, so a cast is the whole translation; these checks keep it honest.
static_assert(static_cast<int>(Wotah::KeyCode::Unknown) == GLFW_KEY_UNKNOWN);
static_assert(static_cast<int>(Wotah::KeyCode::Space) == GLFW_KEY_SPACE);
static_assert(static_cast<int>(Wotah::KeyCode::Digit0) == GLFW_KEY_0);
static_assert(static_cast<int>(Wotah::KeyCode::Digit9) == GLFW_KEY_9);
static_assert(static_cast<int>(Wotah::KeyCode::A) == GLFW_KEY_A);
static_assert(static_cast<int>(Wotah::KeyCode::Z) == GLFW_KEY_Z);
static_assert(static_cast<int>(Wotah::KeyCode::Escape) == GLFW_KEY_ESCAPE);
static_assert(static_cast<int>(Wotah::KeyCode::Enter) == GLFW_KEY_ENTER);
static_assert(static_cast<int>(Wotah::KeyCode::Tab) == GLFW_KEY_TAB);
static_assert(static_cast<int>(Wotah::KeyCode::Backspace) == GLFW_KEY_BACKSPACE);
static_assert(static_cast<int>(Wotah::KeyCode::Delete) == GLFW_KEY_DELETE);
static_assert(static_cast<int>(Wotah::KeyCode::Right) == GLFW_KEY_RIGHT);
static_assert(static_cast<int>(Wotah::KeyCode::Left) == GLFW_KEY_LEFT);
static_assert(static_cast<int>(Wotah::KeyCode::Down) == GLFW_KEY_DOWN);
static_assert(static_cast<int>(Wotah::KeyCode::Up) == GLFW_KEY_UP);
static_assert(static_cast<int>(Wotah::KeyCode::LeftShift) == GLFW_KEY_LEFT_SHIFT);
static_assert(static_cast<int>(Wotah::KeyCode::LeftControl) == GLFW_KEY_LEFT_CONTROL);
static_assert(static_cast<int>(Wotah::KeyCode::LeftAlt) == GLFW_KEY_LEFT_ALT);
static_assert(static_cast<int>(Wotah::KeyCode::RightShift) == GLFW_KEY_RIGHT_SHIFT);
static_assert(static_cast<int>(Wotah::KeyCode::RightControl) == GLFW_KEY_RIGHT_CONTROL);
static_assert(static_cast<int>(Wotah::KeyCode::RightAlt) == GLFW_KEY_RIGHT_ALT);
static_assert(static_cast<int>(Wotah::MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT);
static_assert(static_cast<int>(Wotah::MouseButton::Right) == GLFW_MOUSE_BUTTON_RIGHT);
static_assert(static_cast<int>(Wotah::MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);

namespace
{
    /** Input events room kept from the start, so a busy frame (fast mouse, typing) does not allocate. */
    constexpr std::size_t ExpectedEventsPerFrame = 64;

    /** Sends GLFW's own error reports to the log, so a failing GLFW call always has a reason in the log. */
    void LogGlfwError(int code, const char* description)
    {
        Wotah::Logger::Error("GLFW error {}: {}", code, description != nullptr ? description : "(no description)");
    }

    /** A size in pixels from GLFW's int, which is never negative for sizes but is clamped to be safe. */
    uint32_t ToPixels(int value)
    {
        return static_cast<uint32_t>(std::max(value, 0));
    }

    /** A cursor position from GLFW's doubles. */
    glm::vec2 ToCursorPosition(double x, double y)
    {
        return {static_cast<float>(x), static_cast<float>(y)};
    }
} // namespace

/**
 * GLFW's input callbacks. Each finds its Window through the GLFW window's user pointer (set in Create) and adds one
 * InputEvent; they run inside glfwPollEvents / glfwWaitEvents, on the main thread.
 */
struct Wotah::Window::Callbacks
{
    static Window& From(GLFWwindow* handle) { return *static_cast<Window*>(glfwGetWindowUserPointer(handle)); }

    static glm::vec2 GetCursorPosition(GLFWwindow* handle)
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(handle, &x, &y);
        return ToCursorPosition(x, y);
    }

    static void OnKey(GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/)
    {
        InputEvent event;
        event.Type     = action == GLFW_RELEASE ? InputEventType::KeyReleased : InputEventType::KeyPressed;
        event.Key      = static_cast<KeyCode>(key);
        event.IsRepeat = action == GLFW_REPEAT;
        From(handle).m_inputEvents.push_back(event);
    }

    static void OnCharacter(GLFWwindow* handle, unsigned int codePoint)
    {
        InputEvent event;
        event.Type      = InputEventType::TextEntered;
        event.Character = static_cast<char32_t>(codePoint);
        From(handle).m_inputEvents.push_back(event);
    }

    static void OnMouseButton(GLFWwindow* handle, int button, int action, int /*mods*/)
    {
        InputEvent event;
        event.Type = action == GLFW_RELEASE ? InputEventType::MouseButtonReleased : InputEventType::MouseButtonPressed;
        event.Button         = static_cast<MouseButton>(button);
        event.CursorPosition = GetCursorPosition(handle);
        From(handle).m_inputEvents.push_back(event);
    }

    static void OnCursorMove(GLFWwindow* handle, double x, double y)
    {
        Window&         window   = From(handle);
        const glm::vec2 position = ToCursorPosition(x, y);

        // The first movement (and the first after capturing, when GLFW moves the cursor) has nothing to compare with.
        InputEvent event;
        event.Type           = InputEventType::MouseMoved;
        event.CursorPosition = position;
        event.Delta = window.m_hasLastCursorPosition ? position - window.m_lastCursorPosition : glm::vec2(0.0f);
        window.m_lastCursorPosition    = position;
        window.m_hasLastCursorPosition = true;
        window.m_inputEvents.push_back(event);
    }

    static void OnScroll(GLFWwindow* handle, double x, double y)
    {
        InputEvent event;
        event.Type           = InputEventType::MouseScrolled;
        event.CursorPosition = GetCursorPosition(handle);
        event.Delta          = ToCursorPosition(x, y);
        From(handle).m_inputEvents.push_back(event);
    }

    static void OnFramebufferResize(GLFWwindow* handle, int /*width*/, int /*height*/)
    {
        From(handle).m_wasResized = true; // the new size is read when the swapchain is made again
    }

    static void OnFocus(GLFWwindow* handle, int isFocused)
    {
        if (isFocused == GLFW_TRUE)
        {
            return;
        }
        // Keys let go while another window has the focus never reach us, so everything counts as let go now.
        Window& window                 = From(handle);
        window.m_hasLastCursorPosition = false;
        InputEvent event;
        event.Type = InputEventType::FocusLost;
        window.m_inputEvents.push_back(event);
    }
};

Wotah::Window::~Window()
{
    Destroy();
}

bool Wotah::Window::Create(const WindowSize& size, const std::string& title)
{
    if (size.Width == 0 || size.Height == 0 || size.Width > INT_MAX || size.Height > INT_MAX)
    {
        Logger::Error("The window size {}x{} is not usable", size.Width, size.Height);
        return false;
    }

    // Set before glfwInit, so a failing glfwInit is reported too (GLFW docs, "Error handling").
    glfwSetErrorCallback(LogGlfwError);
    if (!glfwInit())
    {
        Logger::Error("Could not start GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan draws, so no OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);    // the user can resize it; TakeResize tells the renderer
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);     // shown by Show, once everything is ready
    m_window =
        glfwCreateWindow(static_cast<int>(size.Width), static_cast<int>(size.Height), title.c_str(), nullptr, nullptr);
    if (m_window == nullptr)
    {
        Logger::Error("Could not open the window");
        return false;
    }

    // The callbacks find this Window through the user pointer; Window can be neither copied nor moved, so it stays.
    m_inputEvents.reserve(ExpectedEventsPerFrame);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, Callbacks::OnKey);
    glfwSetCharCallback(m_window, Callbacks::OnCharacter);
    glfwSetMouseButtonCallback(m_window, Callbacks::OnMouseButton);
    glfwSetCursorPosCallback(m_window, Callbacks::OnCursorMove);
    glfwSetScrollCallback(m_window, Callbacks::OnScroll);
    glfwSetWindowFocusCallback(m_window, Callbacks::OnFocus);
    glfwSetFramebufferSizeCallback(m_window, Callbacks::OnFramebufferResize);
    return true;
}

void Wotah::Window::Destroy()
{
    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate(); // allowed even when glfwInit never ran (GLFW docs, glfwTerminate)

    m_inputEvents.clear();
    m_hasLastCursorPosition = false;
    m_isCursorCaptured      = false;
    m_wasResized            = false;
}

void Wotah::Window::Show()
{
    glfwShowWindow(m_window);
}

bool Wotah::Window::ShouldClose() const
{
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Wotah::Window::PollEvents()
{
    m_inputEvents.clear(); // keeps its memory
    glfwPollEvents();

    // While the window has nothing to draw into (minimized, or resized down to nothing) no swapchain can be made, so
    // it sleeps until that changes. Closing a minimized window (from the taskbar) leaves it minimized, so the close
    // request also ends the wait; without that check the program would sleep here forever.
    while (!ShouldClose() && (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE || !HasDrawableArea()))
    {
        glfwWaitEvents();
    }
}

bool Wotah::Window::TakeResize() noexcept
{
    const bool wasResized = m_wasResized;
    m_wasResized          = false;
    return wasResized;
}

void Wotah::Window::SetSize(const WindowSize& size)
{
    if (size.Width == 0 || size.Height == 0 || size.Width > INT_MAX || size.Height > INT_MAX)
    {
        Logger::Error("The window size {}x{} is not usable", size.Width, size.Height);
        return;
    }
    glfwSetWindowSize(m_window, static_cast<int>(size.Width), static_cast<int>(size.Height));
}

bool Wotah::Window::HasDrawableArea() const
{
    const WindowSize size = GetFramebufferSize();
    return size.Width > 0 && size.Height > 0;
}

void Wotah::Window::SetCursorCaptured(bool isCaptured)
{
    if (isCaptured == m_isCursorCaptured)
    {
        return;
    }
    m_isCursorCaptured = isCaptured;

    // GLFW moves the cursor when the mode changes, so the next movement starts fresh instead of jumping.
    m_hasLastCursorPosition = false;
    glfwSetInputMode(m_window, GLFW_CURSOR, isCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    // Raw motion skips the system's mouse acceleration, which only feels right for a visible pointer (GLFW docs,
    // "Raw mouse motion"); it only works while the cursor is captured.
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, isCaptured ? GLFW_TRUE : GLFW_FALSE);
    }
}

Wotah::WindowSize Wotah::Window::GetFramebufferSize() const
{
    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    WindowSize size;
    size.Width  = ToPixels(width);
    size.Height = ToPixels(height);
    return size;
}

std::span<const char* const> Wotah::Window::GetRequiredInstanceExtensions() const
{
    uint32_t     count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr)
    {
        Logger::Error("GLFW found no Vulkan support for windows on this system");
        return {};
    }
    return {names, count};
}

VkSurfaceKHR Wotah::Window::CreateSurface(VkInstance instance) const
{
    VkSurfaceKHR   surface = VK_NULL_HANDLE;
    const VkResult result  = glfwCreateWindowSurface(instance, m_window, nullptr, &surface);
    if (result != VK_SUCCESS)
    {
        Logger::Error("Could not create the window's Vulkan surface (VkResult {})", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return surface;
}
