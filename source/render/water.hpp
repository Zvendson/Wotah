/**
 * @file water.hpp
 * The sea: one water surface at one height, reaching everywhere; the land rises out of it where it stands higher.
 * FindWaterHeight tells where the surface is at a place: for putting things above it, and the one place water physics
 * will ask. render/water_renderer.hpp draws it.
 *
 *     const Wotah::Sea sea{0.0f};
 *     const std::optional<float> surface = Wotah::FindWaterHeight(sea, x, z);
 *
 * shortcut: the surface is flat and still, and nothing floats yet. Waves go into FindWaterHeight and into the water's
 * vertex shader (shaders/water.vert), buoyancy into the physics, asking FindWaterHeight; both once water physics comes.
 */
#pragma once

#include <optional>

namespace Wotah
{
    /** The sea: its surface's height. */
    struct Sea
    {
        float Level = 0.0f;
    };

    /**
     * The water surface's height at world x, z (under the land too, where the land hides it); empty for a place that
     * is not a number.
     */
    [[nodiscard]] std::optional<float> FindWaterHeight(const Sea& sea, float x, float z);
} // namespace Wotah
