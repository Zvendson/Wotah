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
#include <cstdint>

namespace
{
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
} // namespace

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);   // fixed size, so the swapchain never needs to be made again
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);     // shown by Show, once everything is ready
    m_window =
        glfwCreateWindow(static_cast<int>(size.Width), static_cast<int>(size.Height), title.c_str(), nullptr, nullptr);
    if (m_window == nullptr)
    {
        Logger::Error("Could not open the window");
        return false;
    }
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
    glfwPollEvents();

    // Closing a minimized window (from the taskbar) leaves it minimized, so the close request also ends the wait;
    // without that check the program would sleep here forever.
    while (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE && !ShouldClose())
    {
        glfwWaitEvents();
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
