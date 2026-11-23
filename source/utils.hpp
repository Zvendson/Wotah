/**
 * @file utils.hpp
 * Small helpers that more than one part of the program can use. Every reusable helper lives here, once.
 *
 *     if (const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory())
 *     {
 *         const std::filesystem::path logFile = *folder / "wotah.log";
 *     }
 */
#pragma once

#include <filesystem>
#include <optional>

namespace Wotah
{
    /** The folder that holds the running executable; empty when the operating system does not tell. */
    std::optional<std::filesystem::path> FindExecutableDirectory();
} // namespace Wotah
