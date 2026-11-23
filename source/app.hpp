/**
 * @file app.hpp
 * The program's entry function, called from main().
 */
#pragma once

namespace Wotah
{
    /**
     * Opens the window, sets up Vulkan, draws until the window is closed and cleans up.
     * Returns the process exit code: 0 on success, 1 when setup failed.
     */
    int RunApp();
} // namespace Wotah
