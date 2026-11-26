/**
 * @file utils.hpp
 * Small helpers that more than one part of the program can use. Every reusable helper lives here, once.
 *
 *     if (const std::optional<std::filesystem::path> folder = Wotah::FindExecutableDirectory())
 *     {
 *         const std::filesystem::path logFile = *folder / "wotah.log";
 *     }
 *
 *     const float hills = Wotah::GetLayeredNoise(place / 200.0f, seed, 5, 0.0f); // about -1 to 1
 */
#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace Wotah
{
    /** The folder that holds the running executable; empty when the operating system does not tell. */
    std::optional<std::filesystem::path> FindExecutableDirectory();

    // ---- Noise: smooth randomness for landscapes and other natural shapes ----
    //
    // Every function here gives the same result as its twin in shaders/noise.glsl, to float rounding, so what the CPU
    // computes (physics, placing things) matches what the GPU draws. The same place and seed always give the same
    // value. Places must stay within about a million of the origin.

    /**
     * Gradient noise (Perlin's kind; Perlin 2002, "Improving Noise") over a plane, with its slope: x is the value,
     * about -1 to 1 and 0 at every place with whole-number coordinates; y and z are how fast it rises along the
     * place's x and y. It changes about once per unit.
     */
    [[nodiscard]] glm::vec3 GetGradientNoise(const glm::vec2& place, uint32_t seed) noexcept;

    /**
     * How much of a detail with this wavelength is kept when detail below smallest is left out: none below smallest,
     * all from twice it, smoothly between (so asking for less detail changes a shape smoothly); all of it when smallest
     * is 0 or less, or not a number.
     */
    [[nodiscard]] float GetLayerShare(float wavelength, float smallest) noexcept;

    /**
     * Layered noise (fractional Brownian motion): layers of gradient noise, each twice as fine and half as strong as
     * the one before and turned a little, so their grids never line up; about -1 to 1, and 0 at the origin. Layers
     * with a wavelength (in the place's units, the first one's is 1) below smallest are left out, those between
     * smallest and twice that faded, so asking for less detail changes the result smoothly. 0 or less: all layers.
     */
    [[nodiscard]] float GetLayeredNoise(const glm::vec2& place, uint32_t seed, int layers, float smallest) noexcept;

    /**
     * Layered noise as water and weather wear land down: each layer counts less where the layers before it are
     * steep, so slopes stay smooth and the detail gathers in valleys and on flat tops (Quilez, "Terrain Raymarching",
     * iquilezles.org: noise derivatives). About -1 to 1, 0 at the origin; layers and smallest as GetLayeredNoise.
     */
    [[nodiscard]] float GetErodedNoise(const glm::vec2& place, uint32_t seed, int layers, float smallest) noexcept;
} // namespace Wotah
