/**
 * @file sea.hpp
 * The sea: one water surface reaching everywhere at its level, with waves running over it; the land rises out of it
 * where it stands higher. Two kinds of waves make it:
 *
 * - The wind's waves: a sum of Gerstner waves (Tessendorf 2001, "Simulating Ocean Water"; Finch 2004, GPU Gems
 *   chapter 1): each lifts the water in a sine and moves it along its way in step, so crests come out sharp and
 *   troughs broad. They are short and run every way; over shallow ground they die out.
 * - The swell: one long train of waves from far away that rolls in onto the beach (the surf). Over the shallowing
 *   floor it slows down and its crests crowd together and turn to the shore (deep water's dispersion, solved for the
 *   depth), it grows (shoaling: the same energy in slower water), its crests sharpen and lean forward, and once it
 *   stands higher than the water is deep it breaks and rolls on as a foaming bore, shrinking with the depth. At the
 *   shore each bore runs up the sand as a thin sheet, slows, stops and runs back down, below the waterline, drawn into
 *   the next wave (the swash). Bigger waves come in sets. Where the swash ran the sand stays wet for a while, and the
 *   highest run-ups leave a line of foam.
 *
 * Everything here is the one place the sea's shape comes from: the physics floats things on it (SampleWater), and the
 * water's and the land's shaders draw it from the same numbers (shaders/sea_waves.glsl and water.vert are its twins;
 * GPU tests keep them equal). render/water_renderer.hpp draws it.
 *
 *     Wotah::Sea sea{0.0f};
 *     sea.Waves   = Wotah::MakeSeaWaves(0.6f, {1.0f, 0.0f});   // 0.6 m waves rolling east
 *     sea.Floor   = beach;                                     // the land: the swell breaks on it
 *     sea.Seconds = worldSeconds;
 *     const std::optional<float> surface = Wotah::FindWaterHeight(sea, x, z);
 */
#pragma once

#include "terrain/terrain_shape.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace Wotah
{
    /**
     * The most wind waves a sea has; shaders/sea_waves.glsl has room for as many (WOTAH_MAX_SEA_WAVES, set in CMake).
     */
    inline constexpr uint32_t MaxSeaWaves = WOTAH_MAX_SEA_WAVES;

    /**
     * How deep water must be for a wave of some height: a wave breaks once it stands higher than 0.78 of the depth
     * (McCowan 1894). The swell breaks there; the wind's waves die out towards it (GetShoalShare).
     */
    inline constexpr float BreakingDepthShare = 0.78f;

    /**
     * The finest detail of the sea floor the wind's waves feel, in metres: they die out by the depth of the broad
     * ground, not of every small bump (terrain/terrain_shape.hpp, GetTerrainHeight's smallest).
     */
    inline constexpr float WaveFloorDetail = 4.0f;

    /** One train of the wind's waves: a Gerstner wave. */
    struct SeaWave
    {
        /** Which way its crests run over the water (x, z), length 1. */
        glm::vec2 Direction{1.0f, 0.0f};
        /** From crest to crest, in metres. */
        float Wavelength = 10.0f;
        /** How far its crests rise over the level and its troughs sink under it, in metres, over deep water. */
        float Amplitude = 0.0f;
        /**
         * How far the water moves back and forth with it, as a share of Amplitude: 0 gives round sine waves, 1 the
         * circles real water runs in deep water (crests as sharp as they come without folding over).
         */
        float Steepness = 0.0f;
        /** Where its crests are at second 0, in radians. */
        float Phase = 0.0f;
    };

    /** The swell: the long waves that roll in onto the beach and break there. */
    struct SeaSwell
    {
        /** Which way it rolls (x, z), length 1; towards the shore to break on it. */
        glm::vec2 Direction{1.0f, 0.0f};
        /** From trough to crest over deep water, in metres; 0: no swell. */
        float Height = 0.0f;
        /** The seconds from one crest to the next. */
        float Period = 8.0f;
        /** Where its crests are at second 0, in radians. */
        float Phase = 0.0f;
    };

    /** The waves of a sea, from MakeSeaWaves: the first Count of Waves run, and the swell. */
    struct SeaWaves
    {
        std::array<SeaWave, MaxSeaWaves> Waves{};
        uint32_t                         Count = 0;
        SeaSwell                         Swell;
    };

    /** The sea: its level, its waves, its clock and the floor the waves feel. */
    struct Sea
    {
        /** The height of the still surface; the waves rise and sink around it. */
        float Level = 0.0f;
        /** The waves; none for a still surface. */
        SeaWaves Waves;
        /** The sea's clock in seconds: where the waves are (the world's time, which stands still when it is paused). */
        double Seconds = 0.0;
        /**
         * The ground under the sea, which the waves feel: the swell breaks on it and runs up its beach. Without one the
         * sea is deep everywhere, the swell a plain long wave.
         */
        std::optional<TerrainShape> Floor;
    };

    /** The water at one place of the surface. */
    struct WaterSample
    {
        /** How high the surface is there. */
        float Height = 0.0f;
        /** Which way the surface faces there, length 1. */
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        /** How fast the water at the surface moves there, in metres per second. */
        glm::vec3 Velocity{0.0f};
    };

    /**
     * A sea of significantHeight metres (the mean height of the highest third of its waves, trough to crest, as wave
     * forecasts give it) rolling towards towards (x, z; need not be of length 1): most of it a swell, whose period
     * grows with its height (higher waves are longer), the rest wind waves around it, shorter, spread to both sides
     * and all as steep as each other. Nothing for a height of 0 or less, or a direction or height that is not a number.
     */
    [[nodiscard]] SeaWaves MakeSeaWaves(float significantHeight, const glm::vec2& towards);

    /** A wind wave's wavenumber: radians per metre, 2 pi over its wavelength. */
    [[nodiscard]] float GetWavenumber(const SeaWave& wave);

    /** How fast a wind wave turns over, in radians per second: deep water's sqrt(g k) for its wavenumber k. */
    [[nodiscard]] float GetWaveFrequency(const SeaWave& wave);

    /** Where a wind wave stands at seconds: its phase in radians, from 0 up to 2 pi, kept exact over long times. */
    [[nodiscard]] float GetWavePhase(const SeaWave& wave, double seconds);

    /** How high the wind's waves can stand anywhere: the sum of their amplitudes, in metres. */
    [[nodiscard]] float GetWaveReach(const SeaWaves& waves);

    /**
     * How much of the wind waves' full height (trough to crest, twice their reach) each metre of depth lets stand:
     * BreakingDepthShare / (2 reach); 0 without them. GetShoalShare uses it, and the shaders get it as it is.
     */
    [[nodiscard]] float GetShoalRate(const SeaWaves& waves);

    /**
     * The share of the wind's waves that runs at place (x, z): 1 over deep water, less where the water is too shallow
     * for them to stand (BreakingDepthShare), none at the shore and on land, where the swell's surf runs. 1 without a
     * floor.
     */
    [[nodiscard]] float GetShoalShare(const Sea& sea, const glm::vec2& place);

    /**
     * Where the water that lies at rest at restPlace (x, z, on the level) is moved by the wind's waves now, as an
     * offset from there: along the water (x, z) and up (y). The twin of shaders/sea_waves.glsl's GetWaveOffset with
     * nothing left out.
     */
    [[nodiscard]] glm::vec3 GetWaveOffset(const Sea& sea, const glm::vec2& restPlace);

    // ---- The swell and its surf ----

    /** How many swell waves make one set: bigger and smaller waves come and go over this many. */
    inline constexpr float SwellSetWaves = 7.0f;

    /** The swell's clock at seconds: the part of its phase time adds, from 0 up to 2 pi SwellSetWaves, exact. */
    [[nodiscard]] float GetSwellClock(const SeaSwell& swell, double seconds);

    /**
     * Where the swell stands at a place: its phase (crests at whole turns, growing seaward), and how it has changed
     * on its way in from deep water.
     */
    struct SurfPoint
    {
        /** The phase in radians: a crest where it is a whole number of turns; it grows seaward. */
        float Phase = 0.0f;
        /** Trough to crest, in metres, here. */
        float Height = 0.0f;
        /** How sharp the crests are and broad the troughs: 1 a round wave, up to about 2.2 just before it breaks. */
        float Peak = 1.0f;
        /** How far the crest leans forward: 0 even, towards 1 a near-wall front (a broken bore). */
        float Lean = 0.0f;
        /** How broken the wave is: 0 not at all, 1 a broken, foaming bore. */
        float Broken = 0.0f;
        /** How much the breaking waves raise the mean water here (wave setup), in metres. */
        float Setup = 0.0f;
        /** How deep the still water is here over the floor's broad profile (very deep without a floor), in metres. */
        float Depth = 0.0f;
    };

    /**
     * The swell at a place given in its frame: seaward metres seaward of the waterline (negative inland) and along
     * metres along the coast (GetSurfFrame), at the swell clock clock (GetSwellClock).
     */
    [[nodiscard]] SurfPoint GetSurfPoint(const Sea& sea, float seaward, float along, float clock);

    /**
     * A place (x, z) in the swell's frame: x along the coast, y seaward of the waterline (terrain/terrain_shape.hpp,
     * GetCoastPlace, at the floor's broad detail); without a floor, along and against the swell's direction.
     */
    [[nodiscard]] glm::vec2 GetSurfFrame(const Sea& sea, const glm::vec2& place);

    /** The swell's height over the mean water (level and setup) at a point: its waveform. */
    [[nodiscard]] float GetSurfWave(const SurfPoint& point);

    /** The swash at a stretch of the shore: where its front is now, and how deep the sheet of water behind it is. */
    struct Swash
    {
        /** Where the front is, in metres seaward of the waterline: the sand seaward of it is under water. */
        float Front = 0.0f;
        /** How deep the sheet of water is just behind the front, at its thinnest, and how much deeper farther back. */
        float Thinnest = 0.0f;
        float Deeper   = 0.0f;
        /** How fast the front runs up the beach, in metres per second; negative while it runs back down. */
        float Speed = 0.0f;
    };

    /** The swash at along metres along the coast, at the swell clock clock; no swash (front far inland) without one. */
    [[nodiscard]] Swash GetSwash(const Sea& sea, float along, float clock);

    /**
     * The water surface's height over a place (x, z) whose sand (the floor's height there) is sand: the still level,
     * the swell's surf and swash, and the wind waves' lift there (windLift, from GetWaveOffset). Where the swash has
     * drawn back from the sand, the surface lies just under it.
     */
    [[nodiscard]] float GetSurfaceHeight(const Sea& sea, const glm::vec2& place, float sand, float windLift);

    /**
     * How wet the sand is at a place (x, z) of the beach: 1 under the water or just left by it, drying over half a
     * minute after the swash last ran over it, 0 where it has not run lately. Out: how much foam the swash left there
     * (the lines where the last run-ups stopped), 0 to 1.
     */
    [[nodiscard]] float GetSandWetness(const Sea& sea, const glm::vec2& place, float& trace);

    /**
     * The water surface at world x, z (under the land too, where the land hides it): its height, which way it faces
     * and how fast the water moves there. Empty for a place that is not a number. Safe to call from several threads at
     * once (the physics does, for its floating bodies).
     */
    [[nodiscard]] std::optional<WaterSample> SampleWater(const Sea& sea, float x, float z);

    /**
     * The water surface's height at world x, z (SampleWater's, without its slope and speed); empty for a place that is
     * not a number.
     */
    [[nodiscard]] std::optional<float> FindWaterHeight(const Sea& sea, float x, float z);
} // namespace Wotah
