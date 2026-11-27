/**
 * @file terrain_shape.hpp
 * The shape of the land, everywhere: a long sandy beach along a wandering coast with the sea to the west (where the
 * sun sets), the sea floor falling away offshore, dunes behind the beach and rolling hills farther inland. One formula
 * and a few numbers, so the world needs no stored data, reaches as far as anyone can travel, and is the same every time
 * for the same numbers. GetTerrainHeight computes it on the CPU (physics, placing things); shaders/terrain_shape.glsl
 * computes the same on the GPU (drawing), from the numbers render/terrain_renderer.hpp hands it.
 *
 *     const Wotah::TerrainShape beach;                              // the defaults are the beach
 *     const float ground = Wotah::GetTerrainHeight(beach, {x, z});  // metres
 *     const Wotah::TerrainPatch patch = Wotah::SampleTerrainPatch(beach, {0.0f, 0.0f}, 1.0f, 256);  // for physics
 *
 * The compass is render/lighting.hpp's: north -z, east +x, south +z, west -x. The coast runs north and south; whatever
 * the seed, the waterline passes through the origin, with land to the east and sea to the west.
 *
 * shortcut: the land is the formula only. Changes made by hand (a paint mode) would be stored layers added on top,
 * read by GetTerrainHeight and its shader twin alike.
 */
#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

namespace Wotah
{
    /** The numbers that shape the land; every length in metres. The defaults make the beach. */
    struct TerrainShape
    {
        /** Picks one of many worlds of this kind: another coast, other dunes and hills. */
        uint32_t Seed = 1;

        /** The sea's surface height the beach is built around: the waterline. */
        float SeaLevel = 0.0f;

        // ---- The coast ----

        /** How far the waterline swings east and west of a straight line, and how far apart the swings are. */
        float CoastWander     = 45.0f;
        float CoastWavelength = 420.0f;

        /** How far the land is bent sideways, so nothing runs in straight lines, and over what distance. */
        float BendStrength   = 24.0f;
        float BendWavelength = 170.0f;

        // ---- The beach and the sea floor ----

        /** How steeply the beach rises at the waterline (0.06: 6 cm a metre); the sea floor falls as steeply. */
        float BeachSlope = 0.06f;

        /** The height above the sea level the beach levels out at inland. */
        float BeachTop = 2.4f;

        /** How deep the sea floor gets far out, below the sea level. */
        float SeaDepth = 16.0f;

        // ---- Dunes: long ridges along the coast behind the beach, giving way to the hills inland ----

        /** How far inland of the waterline the dunes begin, and over what distance they grow to full height. */
        float DuneStart = 25.0f;
        float DuneRamp  = 30.0f;

        /** How high a dune's crest stands. */
        float DuneHeight = 3.5f;

        /** A dune's size along the coast and across it. */
        float DuneLength = 38.0f;
        float DuneWidth  = 16.0f;

        // ---- Hills farther inland ----

        /** How far inland the hills begin, and over what distance they grow to full height. */
        float HillStart = 70.0f;
        float HillRamp  = 150.0f;

        /** How high the hills get, and how far apart they are. */
        float HillHeight     = 18.0f;
        float HillWavelength = 260.0f;

        // ---- Small bumps everywhere ----

        /** How high the bumps that keep the ground from looking machine-made are, and how far apart. */
        float BumpHeight     = 0.25f;
        float BumpWavelength = 14.0f;

        /** The finest detail the land has: wavelengths shorter than this are left out everywhere. */
        float FinestWavelength = 1.0f;
    };

    /**
     * Whether a shape can be computed: every number finite, every wavelength, ramp and the beach slope above 0, every
     * height and depth 0 or more. GetTerrainHeight expects a usable shape; the renderer refuses others (logged).
     */
    [[nodiscard]] bool IsUsable(const TerrainShape& shape);

    /** Land flat at height everywhere: no beach, dunes, hills or bumps (for tests, or a level ground). */
    [[nodiscard]] TerrainShape MakeFlatTerrainShape(float height);

    /**
     * The ground's height at a place (x, z). Detail finer than smallest metres is left out; drawing asks for less
     * far away (a coarser smallest), so the far land stays calm instead of flickering, and the change is smooth. 0
     * (the default) keeps all the shape has.
     */
    [[nodiscard]] float GetTerrainHeight(const TerrainShape& shape, const glm::vec2& place, float smallest = 0.0f);

    /**
     * A place in the coast's own frame, which the land's formula works in: bent a little sideways (so nothing runs in
     * straight lines); x of Bent runs along the coast (towards the south), y seaward (towards the west); Seaward is how
     * far seaward of the wandering waterline the place lies, in metres (negative inland). The sea's waves feel the
     * beach by it.
     */
    struct CoastPlace
    {
        glm::vec2 Bent{0.0f};
        float     Seaward = 0.0f;
    };

    /** Where a place (x, z) lies in the coast's frame, leaving out detail finer than smallest metres. */
    [[nodiscard]] CoastPlace GetCoastPlace(const TerrainShape& shape, const glm::vec2& place, float smallest = 0.0f);

    /**
     * How deep the still sea stands over the beach's broad profile (no dunes, hills or bumps) seaward metres seaward of
     * the waterline; negative inland: the beach's height over the sea there.
     */
    [[nodiscard]] float GetBeachDepth(const TerrainShape& shape, float seaward);

    /**
     * Heights on a square grid: SamplesPerSide x SamplesPerSide of them, Spacing apart, the first at Corner (x, z),
     * row by row from -z to +z, each row from -x to +x.
     */
    struct TerrainPatch
    {
        glm::vec2          Corner{0.0f};
        float              Spacing        = 0.0f;
        uint32_t           SamplesPerSide = 0;
        std::vector<float> Heights;
    };

    /**
     * The land's heights on a square grid around center, samplesPerSide to a side, spacing apart (for physics). Empty
     * (and logged) for a shape that is not usable, a spacing that is not above 0, or fewer than 2 or more than 4096
     * samples to a side.
     */
    [[nodiscard]] TerrainPatch SampleTerrainPatch(const TerrainShape& shape, const glm::vec2& center, float spacing,
                                                  uint32_t samplesPerSide);

    /**
     * SampleTerrainPatch in two parts, so parts of a patch can be sampled on several threads at once: MakeTerrainPatch
     * lays the grid out (its heights all 0; empty and logged as SampleTerrainPatch), SampleTerrainRows samples the
     * rows from firstRow up to endRow (not included; rows past the patch are skipped). Different rows of one patch may
     * be sampled on different threads at the same time.
     */
    [[nodiscard]] TerrainPatch MakeTerrainPatch(const TerrainShape& shape, const glm::vec2& center, float spacing,
                                                uint32_t samplesPerSide);
    void SampleTerrainRows(const TerrainShape& shape, TerrainPatch& patch, uint32_t firstRow, uint32_t endRow);
} // namespace Wotah
