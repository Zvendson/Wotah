/**
 * @file terrain_shape.cpp
 * The land's formula. It mirrors shaders/terrain_shape.glsl line by line; a change to one must go into the other
 * (tests/gpu_tests.cpp compares them).
 */
#include "terrain/terrain_shape.hpp"

#include "core/logger.hpp"
#include "core/utils.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace
{
    /**
     * How many noise layers each feature has, and the salts that give each feature noise of its own from one seed.
     * shaders/terrain_shape.glsl uses the same.
     */
    constexpr int      BendLayers  = 3;
    constexpr int      CoastLayers = 4;
    constexpr int      HillLayers  = 6;
    constexpr int      BumpLayers  = 3;
    constexpr uint32_t BendXSalt   = 0x68E31DA4u;
    constexpr uint32_t BendZSalt   = 0xB5297A4Du;
    constexpr uint32_t CoastSalt   = 0x1B56C4E9u;
    constexpr uint32_t DuneSalt    = 0x7FEB352Du;
    constexpr uint32_t HillSalt    = 0x846CA68Bu;
    constexpr uint32_t BumpSalt    = 0x2C1B3C6Du;

    /**
     * A feature's own seed: the shape's seed spread by the golden ratio's 32-bit fraction (Knuth's multiplicative
     * hashing), so neighbouring seeds give unrelated worlds, then mixed with the feature's salt.
     */
    constexpr uint32_t SeedSpread = 0x9E3779B9u;

    uint32_t MixSeed(uint32_t seed, uint32_t salt) noexcept
    {
        return (seed * SeedSpread) ^ salt;
    }

    /**
     * Rises from 0 as steeply as slope and levels out at limit: limit (1 - e^(-distance slope / limit)); 0 for a limit
     * of 0 or less.
     */
    float Approach(float distance, float slope, float limit) noexcept
    {
        if (!(limit > 0.0f))
        {
            return 0.0f;
        }
        return limit * (1.0f - std::exp(-distance * slope / limit));
    }

    /**
     * The beach and the sea floor by how far seaward of the waterline a place lies (negative inland): rising inland and
     * falling offshore, both as steeply as BeachSlope at the waterline (so the ground has no crease there), levelling
     * out at BeachTop and SeaDepth.
     */
    float GetBeachProfile(const Wotah::TerrainShape& shape, float seaward) noexcept
    {
        if (seaward >= 0.0f)
        {
            return -Approach(seaward, shape.BeachSlope, shape.SeaDepth);
        }
        return Approach(-seaward, shape.BeachSlope, shape.BeachTop);
    }

    /**
     * A place (x, z) in the coast's own frame, which the formula works in: x along the coast (towards the south), y
     * seaward (towards the west), so the sun sets over the sea (owner's wish). A quarter turn, nothing mirrored.
     */
    glm::vec2 ToCoastFrame(const glm::vec2& place) noexcept
    {
        return {place.y, -place.x};
    }

    /** The largest number of samples to a side SampleTerrainPatch makes: 4096 x 4096 floats are 64 MiB. */
    constexpr uint32_t MaxPatchSamples = 4096;
} // namespace

bool Wotah::IsUsable(const TerrainShape& shape)
{
    const auto isPositive = [](float value)
    {
        return value > 0.0f && std::isfinite(value);
    };
    const auto isAmount = [](float value)
    {
        return value >= 0.0f && std::isfinite(value);
    };
    const std::array lengths{shape.CoastWavelength, shape.BendWavelength,  shape.BeachSlope, shape.DuneRamp,
                             shape.DuneLength,      shape.DuneWidth,       shape.HillRamp,   shape.HillWavelength,
                             shape.BumpWavelength,  shape.FinestWavelength};
    const std::array amounts{shape.CoastWander, shape.BendStrength, shape.BeachTop,  shape.SeaDepth, shape.DuneHeight,
                             shape.HillHeight,  shape.BumpHeight,   shape.DuneStart, shape.HillStart};
    return std::isfinite(shape.SeaLevel) && std::ranges::all_of(lengths, isPositive) &&
           std::ranges::all_of(amounts, isAmount);
}

Wotah::TerrainShape Wotah::MakeFlatTerrainShape(float height)
{
    TerrainShape flat;
    flat.SeaLevel     = height;
    flat.CoastWander  = 0.0f;
    flat.BendStrength = 0.0f;
    flat.BeachTop     = 0.0f;
    flat.SeaDepth     = 0.0f;
    flat.DuneHeight   = 0.0f;
    flat.HillHeight   = 0.0f;
    flat.BumpHeight   = 0.0f;
    return flat;
}

Wotah::CoastPlace Wotah::GetCoastPlace(const TerrainShape& shape, const glm::vec2& place, float smallest)
{
    const float     detail = std::max(smallest, shape.FinestWavelength);
    const glm::vec2 coast  = ToCoastFrame(place);

    // Bend the land sideways a little, so coast, dunes and hills do not run in straight lines.
    const glm::vec2 bendPlace  = coast / shape.BendWavelength;
    const float     bendDetail = detail / shape.BendWavelength;
    const glm::vec2 bend =
        glm::vec2(GetLayeredNoise(bendPlace, MixSeed(shape.Seed, BendXSalt), BendLayers, bendDetail),
                  GetLayeredNoise(bendPlace, MixSeed(shape.Seed, BendZSalt), BendLayers, bendDetail)) *
        shape.BendStrength;
    CoastPlace frame;
    frame.Bent = coast + bend;

    // The waterline wanders to and fro along the coast; how far seaward of it the place lies.
    const glm::vec2 coastPlace(frame.Bent.x / shape.CoastWavelength, 0.0f);
    const float     shore = shape.CoastWander * GetLayeredNoise(coastPlace, MixSeed(shape.Seed, CoastSalt), CoastLayers,
                                                                detail / shape.CoastWavelength);
    frame.Seaward         = frame.Bent.y - shore;
    return frame;
}

float Wotah::GetBeachDepth(const TerrainShape& shape, float seaward)
{
    return -GetBeachProfile(shape, seaward);
}

float Wotah::GetTerrainHeight(const TerrainShape& shape, const glm::vec2& place, float smallest)
{
    const float      detail  = std::max(smallest, shape.FinestWavelength);
    const CoastPlace frame   = GetCoastPlace(shape, place, smallest);
    const glm::vec2  bent    = frame.Bent;
    const float      seaward = frame.Seaward;
    const float      inland  = -seaward;
    float            height  = shape.SeaLevel + GetBeachProfile(shape, seaward);

    // Dunes behind the beach, giving way to the hills inland: long ridges along the coast, rounded on top (a crest from
    // 1 - |noise|, squared).
    const float hillZone = glm::smoothstep(shape.HillStart, shape.HillStart + shape.HillRamp, inland);
    const float duneZone =
        glm::smoothstep(shape.DuneStart, shape.DuneStart + shape.DuneRamp, inland) * (1.0f - hillZone);
    if (duneZone > 0.0f)
    {
        const glm::vec2 dunePlace(bent.x / shape.DuneLength, bent.y / shape.DuneWidth);
        const float     crest = 1.0f - std::abs(GetGradientNoise(dunePlace, MixSeed(shape.Seed, DuneSalt)).x);
        height += shape.DuneHeight * duneZone * crest * crest * GetLayerShare(shape.DuneWidth, detail);
    }

    // Hills farther inland, worn down as water and weather would.
    if (hillZone > 0.0f)
    {
        const float worn = GetErodedNoise(bent / shape.HillWavelength, MixSeed(shape.Seed, HillSalt), HillLayers,
                                          detail / shape.HillWavelength);
        height += shape.HillHeight * hillZone * (0.5f + 0.5f * worn);
    }

    // Small bumps everywhere, under the water too.
    height += shape.BumpHeight * GetLayeredNoise(bent / shape.BumpWavelength, MixSeed(shape.Seed, BumpSalt), BumpLayers,
                                                 detail / shape.BumpWavelength);
    return height;
}

Wotah::TerrainPatch Wotah::SampleTerrainPatch(const TerrainShape& shape, const glm::vec2& center, float spacing,
                                              uint32_t samplesPerSide)
{
    if (!IsUsable(shape) || !(spacing > 0.0f) || !std::isfinite(spacing) || samplesPerSide < 2 ||
        samplesPerSide > MaxPatchSamples)
    {
        Logger::Error("A terrain patch needs a usable shape, a spacing above 0 and 2 to {} samples to a side, not {} "
                      "and {}",
                      MaxPatchSamples, spacing, samplesPerSide);
        return {};
    }

    TerrainPatch patch;
    patch.Spacing        = spacing;
    patch.SamplesPerSide = samplesPerSide;
    patch.Corner         = center - glm::vec2(0.5f * static_cast<float>(samplesPerSide - 1) * spacing);
    patch.Heights.resize(std::size_t{samplesPerSide} * samplesPerSide);
    for (uint32_t row = 0; row < samplesPerSide; ++row)
    {
        for (uint32_t column = 0; column < samplesPerSide; ++column)
        {
            const glm::vec2 place =
                patch.Corner + glm::vec2(static_cast<float>(column), static_cast<float>(row)) * spacing;
            patch.Heights[std::size_t{row} * samplesPerSide + column] = GetTerrainHeight(shape, place);
        }
    }
    return patch;
}
