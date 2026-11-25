/**
 * @file utils.hpp
 * Small helpers that more than one part of the program can use. Every reusable helper lives here, once.
 *
 *     if (const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory())
 *     {
 *         const std::filesystem::path logFile = *folder / "wotah.log";
 *     }
 *     angle = Wotah::TurnAngle(angle, radiansPerSecond, frameSeconds);
 */
#pragma once

#include <filesystem>
#include <optional>

namespace Wotah
{
    /** The folder that holds the running executable; empty when the operating system does not tell. */
    std::optional<std::filesystem::path> FindExecutableDirectory();

    /**
     * An angle in radians turned on by radiansPerSecond for seconds (a negative speed turns back), kept within
     * [0, 2 pi) so it never grows until floats lose precision. A turn that is not a finite number leaves the angle as
     * it was.
     */
    [[nodiscard]] float TurnAngle(float angle, float radiansPerSecond, float seconds) noexcept;
} // namespace Wotah
