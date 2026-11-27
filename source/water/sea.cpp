/**
 * @file sea.cpp
 * The sea's waves: the wind's waves and the swell, how they are made, where they stand at a time, how the swell breaks
 * and runs up the beach, and the surface they make at a place. shaders/sea_waves.glsl mirrors the swell's part line
 * by line, water.vert the surface; a change to one must go into the other (tests/gpu_tests.cpp compares them).
 */
#include "water/sea.hpp"

#include "core/utils.hpp"
#include "physics/physics_world.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

    // ---- How the sea is made ----

    /**
     * How the significant height splits into swell and wind waves (their energies add up: 0.85^2 + 0.53^2 = 1). Most
     * of a beach's surf comes from swell; by eye.
     */
    constexpr float SwellShare = 0.85f;
    constexpr float WindShare  = 0.53f;

    /**
     * How long the swell's period is for its height: about 5 seconds per square root of metre, as a fully grown sea's
     * peak (Pierson and Moskowitz 1964: Tp = 4.99 sqrt(Hs)), and never shorter than MinSwellPeriod: a swell that came
     * from far away is long even when low. So 0.5 m waves come every 7 seconds, 3 m ones every 8.7, 118 m apart.
     */
    constexpr float PeriodPerRootHeight = 4.99f;
    constexpr float MinSwellPeriod      = 7.0f;

    /** One wind wave before its size: its length as a share of the wind sea's peak, how far it turns, by eye. */
    struct WaveForm
    {
        float Share       = 1.0f; // of the peak wavelength; also its weight, so all are as steep
        float TurnDegrees = 0.0f; // from the way the sea rolls, counter-clockwise seen from above
    };

    /**
     * The wind waves MakeSeaWaves makes: the wind sea's peak and ever shorter waves turned ever farther to both sides.
     * Each weight grows with its wavelength, so all are about equally steep, as wind waves are (their slope spectrum is
     * flat: Phillips 1958). Wavelengths stay above MinWindWavelength, which the nearest part of the drawn sea still
     * shows (render/water_renderer.hpp); finer waves are the shaders' ripples.
     */
    constexpr std::array<WaveForm, 8> WaveForms{{
        {1.0f, 0.0f},
        {0.7f, -14.0f},
        {0.5f, 18.0f},
        {0.35f, -27.0f},
        {0.25f, 33.0f},
        {0.17f, -40.0f},
        {0.12f, 24.0f},
        {0.085f, -15.0f},
    }};
    static_assert(WaveForms.size() <= Wotah::MaxSeaWaves, "the made waves must fit the sea");
    constexpr float MinWindWavelength = 2.0f;

    /**
     * How far all the wind waves' sideways motion together may go before a crest would fold over itself: the sum of
     * each wave's Steepness x wavenumber x amplitude stays below 1 (Finch 2004); 0.8 keeps the sharpest crests rounded.
     */
    constexpr float FoldLimit = 0.8f;

    /** A sea's significant height from its waves' amplitudes: 4 standard deviations of the surface (2 sqrt 2 sqrt(sum
     * a^2)). */
    constexpr float SignificantPerRootSquares = 2.0f * std::numbers::sqrt2_v<float>;

    /** How often SampleWater steps back from a place to the water that moved there: enough for 1 mm on calm seas. */
    constexpr int RestPlaceSteps = 8;

    /** The golden angle, 2 pi (1 - 1 / golden ratio) radians: phases spread by it never line up. */
    constexpr float GoldenAngle = 2.39996323f;

    // ---- The swell over the shallowing floor ----

    /** How much bigger and smaller the waves of a set get: the swell's height times 1 +- this. */
    constexpr float SetDepth = 0.3f;

    /**
     * How much the swell's height changes along the coast (so peaks form and the surf is not a ruler line), and over
     * how long a stretch, in metres; the noise's own salt. By eye.
     */
    constexpr float    AlongVariation  = 0.2f;
    constexpr float    AlongWavelength = 90.0f;
    constexpr uint32_t AlongSalt       = 0x5BD1E995u;

    /** How far off the shore the swell may come in: its direction's share along the coast, at most; more is turned. */
    constexpr float MaxAlongShare = 0.8f;

    /** The water deeper than any wave feels, where the sea has no floor, in metres. */
    constexpr float NoFloorDepth = 1.0e4f;

    /** The shallowest water the swell's wavenumber is worked out for (it grows without end towards no depth). */
    constexpr float ShallowestDepth = 0.02f;

    /** The most a wave grows over shallow water (shoaling); it breaks long before. */
    constexpr float MaxShoaling = 2.5f;

    /**
     * Where the swell breaks: from BreakStart of the height the depth lets stand (BreakingDepthShare) it starts, by
     * BreakEnd it is a broken bore, standing BrokenDepthShare of the depth high (a broken wave keeps about half the
     * depth's height through the surf: Thornton and Guza 1983 find 0.42).
     */
    constexpr float BreakStart       = 0.9f;
    constexpr float BreakEnd         = 1.3f;
    constexpr float BrokenDepthShare = 0.5f;

    /**
     * How the swell's shape changes as it nears breaking (the share of the breaking height it stands at): its crests
     * sharpen and troughs flatten from PeakStart up to 1 + PeakGrowth (cnoidal waves), its front steepens from
     * LeanStart up to MaxLean; once broken the front is a near-wall (BoreLean). By eye after photographs.
     */
    constexpr float PeakStart  = 0.35f;
    constexpr float PeakGrowth = 1.2f;
    constexpr float LeanStart  = 0.6f;
    constexpr float MaxLean    = 0.85f;
    constexpr float BoreLean   = 0.9f;

    /** How much the broken waves raise the mean water at the shore, as a share of the swell's height (wave setup). */
    constexpr float SetupShare = 0.15f;

    /**
     * The nodes and weights of 8-point Gauss-Legendre integration over 0 to 1: the swell's phase is the wavenumber
     * summed over the way in (GetPhaseAcross).
     */
    constexpr std::array<float, 8> QuadratureNodes{0.01985507f, 0.10166676f, 0.23723380f, 0.40828268f,
                                                   0.59171732f, 0.76276620f, 0.89833324f, 0.98014493f};
    constexpr std::array<float, 8> QuadratureWeights{0.05061427f, 0.11119052f, 0.15685332f, 0.18134189f,
                                                     0.18134189f, 0.15685332f, 0.11119052f, 0.05061427f};

    // ---- The swash ----

    /**
     * How high each wave runs up the beach (Stockdon et al. 2006, the run-up 2 % of waves pass, from the deep height
     * and wavelength and the beach's slope): 1.1 (0.35 slope sqrt(H L) + sqrt(H L (0.563 slope^2 + 0.004)) / 2).
     */
    constexpr float RunUpFactor = 1.1f;
    constexpr float SetupFactor = 0.35f;
    constexpr float SlopeFactor = 0.563f;
    constexpr float SwashFloor  = 0.004f;

    /**
     * How the swash runs: it slows by SwashSlowing metres per second each second (gravity down the beach and the sand's
     * drag), runs up and back within SwashShare of the wave's period (else the next wave is there; then it runs less
     * far), and draws back DrawShare of its run-up below the waterline before the next wave (the "suck"). By eye.
     */
    constexpr float SwashSlowing = 1.2f;
    constexpr float SwashShare   = 0.85f;
    constexpr float DrawShare    = 0.5f;

    /**
     * The sheet of water the swash spreads: SheetThinnest metres at its front, SheetShare of the run-up deeper
     * SheetRamp metres behind it as it comes up, thinning to nothing as it runs back. Where the swash has drawn back,
     * the water's surface lies SandCover metres under the sand, out of sight; but up to FrontMargin metres landward of
     * the front it stays the thinnest sheet over it, so the water's shader can cut it off at the front itself, pixel
     * by pixel (the edge where a nearly flat sheet meets the sand would otherwise run along the grid's triangles).
     */
    constexpr float SheetThinnest = 0.02f;
    constexpr float SheetShare    = 0.3f;
    constexpr float SheetRamp     = 3.0f;

    /**
     * How long a new wave's sheet takes to rise to its full depth as its bore comes in, in seconds: the water does not
     * jump up at once (it once did, and the whole swash shook with every wave). By eye.
     */
    constexpr float SheetRise   = 0.6f;
    constexpr float SandCover   = 0.05f;
    constexpr float FrontMargin = 2.0f;

    /**
     * Deeper than this (metres of still water over the floor's broad profile) the swash never reaches: the sand lies
     * too far under even a broken wave's trough for the sheet to show, so it is not worked out.
     */
    constexpr float SwashDepth = 3.0f;

    /**
     * Wet sand: it dries over DryingSeconds after the swash left it (half wet after 0.7 of it); the water soaks
     * WetEdge metres farther up than a run-up reached, less the farther; the foam a run-up leaves where it stopped
     * lasts TraceSeconds and is TraceWidth metres wide. The last WetWaves waves count.
     */
    constexpr float DryingSeconds = 25.0f;
    constexpr float TraceSeconds  = 15.0f;
    constexpr float TraceWidth    = 0.25f;
    constexpr float WetEdge       = 0.5f;
    constexpr int   WetWaves      = 6;

    /** The fastest water the swell moves (the physics' drag), in metres per second. */
    constexpr float MaxSurfSpeed = 3.0f;

    /**
     * How much less the surface water of a broken wave runs back under its trough than it runs in under its crest: the
     * water a bore brings in flows back out along the floor (the undertow), so what floats is carried to the beach.
     * By eye.
     */
    constexpr float UndertowShare = 0.6f;

    /**
     * A broken wave's roller: the foaming water on its front and just behind its crest (falling off over RollerTrail of
     * the wave behind it) runs on as fast as the bore itself, carrying what floats with it, as whitewater carries a
     * bodyboarder. By eye.
     */
    constexpr float RollerTrail = 0.15f;

    /** How far to each side SampleWater looks for the way to the shore, in metres. */
    constexpr float ShoreReach = 0.5f;

    /** How far to each side SampleWater looks for the surface's slope, in metres, and in time for its speed. */
    constexpr float  SlopeReach = 0.05f;
    constexpr double SpeedReach = 0.02;

    // ---- The wind's waves ----

    /** The water at the surface over the rest place, all at once: the wind waves' part. */
    struct WaveState
    {
        glm::vec3 Offset{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        glm::vec3 Velocity{0.0f};
    };

    /** The water that lies at rest at restPlace: where it is now, which way the surface faces there and how it moves.
     */
    WaveState GetWaveState(const Wotah::Sea& sea, const glm::vec2& restPlace)
    {
        const float shoal = Wotah::GetShoalShare(sea, restPlace);
        WaveState   state;
        glm::vec3   tilt{0.0f};
        for (uint32_t index = 0; index < sea.Waves.Count; ++index)
        {
            const Wotah::SeaWave& wave      = sea.Waves.Waves[index];
            const float           number    = Wotah::GetWavenumber(wave);
            const float           amplitude = shoal * wave.Amplitude;

            // Whole wavelengths dropped first, so the sine's angle stays small and exact far from the origin (the
            // shader does the same: its sine is only exact for small angles).
            const float cycles = glm::dot(wave.Direction, restPlace) / wave.Wavelength;
            const float angle  = TwoPi * (cycles - std::floor(cycles)) + Wotah::GetWavePhase(wave, sea.Seconds);
            const float along  = std::cos(angle);
            const float across = std::sin(angle);

            // Up and down in a sine, back and forth along the wave in step: crests gather the water, troughs spread it.
            state.Offset += glm::vec3(wave.Direction.x * wave.Steepness * amplitude * along, amplitude * across,
                                      wave.Direction.y * wave.Steepness * amplitude * along);

            // The surface's tilt (GPU Gems 1, chapter 1, equation 12) and the water's motion (the offset's change).
            const float slope = number * amplitude;
            tilt += glm::vec3(wave.Direction.x * slope * along, wave.Steepness * slope * across,
                              wave.Direction.y * slope * along);
            const float speed = amplitude * Wotah::GetWaveFrequency(wave);
            state.Velocity += glm::vec3(wave.Direction.x * wave.Steepness * speed * across, -speed * along,
                                        wave.Direction.y * wave.Steepness * speed * across);
        }
        state.Normal = glm::normalize(glm::vec3(-tilt.x, 1.0f - tilt.y, -tilt.z));
        return state;
    }

    // ---- The swell ----

    /** The swell's angular frequency, radians per second. */
    float GetSwellFrequency(const Wotah::SeaSwell& swell)
    {
        return TwoPi / swell.Period;
    }

    /** The swell's wavenumber over deep water: frequency^2 / g. */
    float GetDeepNumber(const Wotah::SeaSwell& swell)
    {
        const float frequency = GetSwellFrequency(swell);
        return frequency * frequency / Wotah::StandardGravity;
    }

    /** Whether the swell breaks on the sea's floor: there is a floor, a swell, and it rolls towards the shore (+x). */
    bool HasSurf(const Wotah::Sea& sea)
    {
        return sea.Floor.has_value() && sea.Waves.Swell.Height > 0.0f && sea.Waves.Swell.Direction.x > 0.0f;
    }

    /** The swell's wavenumber along the coast: its direction's share along it (Snell's law keeps it on the way in). */
    float GetAlongNumber(const Wotah::Sea& sea, float deep)
    {
        if (!sea.Floor)
        {
            return 0.0f; // the frame runs with the swell
        }
        return deep * std::clamp(sea.Waves.Swell.Direction.y, -MaxAlongShare, MaxAlongShare);
    }

    /** The swell's wavenumber over water depth deep (Eckart 1952's solution of the dispersion relation). */
    float GetLocalNumber(float deep, float depth)
    {
        return deep / std::sqrt(std::tanh(deep * std::max(depth, ShallowestDepth)));
    }

    /** How much a wave grows over water depth deep: sqrt(deep water's group speed / here), at most MaxShoaling. */
    float GetShoaling(float deep, float local, float depth, float frequency)
    {
        const float twice = 2.0f * local * std::max(depth, ShallowestDepth);
        const float share = 0.5f * (1.0f + (twice < 20.0f ? twice / std::sinh(twice) : 0.0f));
        const float group = frequency / local * share;
        const float far   = frequency / (2.0f * deep);
        return std::min(std::sqrt(far / group), MaxShoaling);
    }

    /** How deep the still water is seaward metres seaward of the waterline: the floor's broad profile. */
    float GetSwellDepth(const Wotah::Sea& sea, float seaward)
    {
        return sea.Floor ? Wotah::GetBeachDepth(*sea.Floor, seaward) : NoFloorDepth;
    }

    /**
     * The swell's phase across the coast at seaward: its wavenumber across the coast summed from the waterline out
     * (Gauss-Legendre over seaward w^2, so the sum stays smooth where the wavenumber grows towards the shore). 0 on
     * land.
     */
    float GetPhaseAcross(const Wotah::Sea& sea, float seaward, float deep, float alongNumber)
    {
        if (!sea.Floor)
        {
            return deep * seaward;
        }
        if (!(seaward > 0.0f))
        {
            return 0.0f;
        }
        float sum = 0.0f;
        for (std::size_t index = 0; index < QuadratureNodes.size(); ++index)
        {
            const float node   = QuadratureNodes[index];
            const float local  = GetLocalNumber(deep, Wotah::GetBeachDepth(*sea.Floor, seaward * node * node));
            const float across = std::sqrt(std::max(local * local - alongNumber * alongNumber, 0.04f * local * local));
            sum += QuadratureWeights[index] * across * 2.0f * seaward * node;
        }
        return sum;
    }

    /** How much bigger the swell's waves are at a phase and a place along the coast: the sets and the peaks. */
    float GetSetShare(float phase, float along)
    {
        const float peaks =
            1.0f + AlongVariation * Wotah::GetGradientNoise({along / AlongWavelength, 0.0f}, AlongSalt).x;
        return (1.0f + SetDepth * std::sin(phase / Wotah::SwellSetWaves)) * peaks;
    }

    /** The phase of the crest that last passed a place with phase phase. */
    float GetLastCrest(float phase)
    {
        return phase - TwoPi * (phase / TwoPi - std::floor(phase / TwoPi));
    }

    /**
     * How high a wave of height (deep, trough to crest) and period runs up a beach of slope, in metres over the mean
     * water (Stockdon et al. 2006).
     */
    float GetRunUp(float height, float period, float slope)
    {
        const float deepLength = Wotah::StandardGravity * period * period / TwoPi;
        const float root       = std::sqrt(height * deepLength);
        return RunUpFactor *
               (SetupFactor * slope * root + 0.5f * root * std::sqrt(SlopeFactor * slope * slope + SwashFloor));
    }

    /** One wave's swash: where the mean waterline is, how far it runs up and draws back, and when. */
    struct SwashCycle
    {
        float Shore    = 0.0f; // the mean waterline with the setup, metres seaward (negative: up the beach)
        float Up       = 0.0f; // how far it runs up the beach from there, metres
        float Down     = 0.0f; // how far it draws back below it, metres
        float PeakTime = 0.0f; // seconds from its start to its highest point
        float Since    = 0.0f; // seconds since it started (its bore reached the drawn-back waterline)
        float RunUp    = 0.0f; // its run-up, metres of height
    };

    /**
     * The swash of the wave that is waves waves back (0: the latest) at along, swell clock clock: the latest wave
     * whose bore has reached the drawn-back waterline.
     */
    /** A swash's size from its wave's height: run-up, setup, how far up and back it runs, when it peaks. */
    SwashCycle GetSwashSize(float height, float period, float slope)
    {
        SwashCycle cycle;
        cycle.RunUp      = GetRunUp(height, period, slope);
        cycle.Shore      = -SetupShare * height / slope;
        const float up   = cycle.RunUp / slope;
        const float down = DrawShare * up;
        const float most = 0.5f * SwashSlowing * std::pow(0.5f * SwashShare * period, 2.0f);
        const float fit  = std::min(1.0f, most / (up + down));
        cycle.Up         = fit * up;
        cycle.Down       = fit * down;
        cycle.PeakTime   = std::sqrt(2.0f * (cycle.Up + cycle.Down) / SwashSlowing);
        return cycle;
    }

    SwashCycle GetSwashCycle(const Wotah::Sea& sea, float along, float clock, int waves)
    {
        const Wotah::SeaSwell& swell       = sea.Waves.Swell;
        const float            slope       = sea.Floor->BeachSlope;
        const float            deep        = GetDeepNumber(swell);
        const float            alongNumber = GetAlongNumber(sea, deep);

        // The clock: each wave's swash starts as its crest reaches the drawn-back waterline (of a wave of the swell's
        // own height, so every wave keeps to one clock), before it reaches the shore.
        const SwashCycle usual   = GetSwashSize(swell.Height, swell.Period, slope);
        const float      drawn   = std::max(usual.Shore + usual.Down, 0.0f);
        const float      atDrawn = -alongNumber * along + clock + GetPhaseAcross(sea, drawn, deep, alongNumber);
        const float      turns   = atDrawn / TwoPi - std::floor(atDrawn / TwoPi);

        // The wave waves back from the last one there: it starts and ends where every wave does (the usual mean and
        // drawn-back waterline, so the front never jumps from one wave to the next) and runs as far up as its own size
        // lets it (its crest's place in the set), within the time a wave has.
        const float      crest = GetLastCrest(atDrawn) - TwoPi * static_cast<float>(waves);
        const SwashCycle own   = GetSwashSize(swell.Height * GetSetShare(crest, along), swell.Period, slope);
        const float      most  = 0.5f * SwashSlowing * std::pow(0.5f * SwashShare * swell.Period, 2.0f);
        SwashCycle       cycle = usual;
        cycle.RunUp            = own.RunUp;
        cycle.Up               = std::min(own.Up, most - usual.Down);
        cycle.PeakTime         = std::sqrt(2.0f * (cycle.Up + cycle.Down) / SwashSlowing);
        cycle.Since            = (turns + static_cast<float>(waves)) * swell.Period;
        return cycle;
    }

    /**
     * How much of its full depth the sheet of cycle has now: rising as its bore comes in, then thinning to nothing as
     * it runs back.
     */
    float GetSheetFreshness(const SwashCycle& cycle)
    {
        return std::min(cycle.Since / SheetRise, 1.0f) * std::max(0.0f, 1.0f - cycle.Since / (2.0f * cycle.PeakTime));
    }

    /** How far up the beach from its mean waterline the swash of cycle is, seconds into it, in metres. */
    float GetSwashReach(const SwashCycle& cycle, float seconds)
    {
        if (seconds >= 2.0f * cycle.PeakTime)
        {
            return -cycle.Down;
        }
        const float early = seconds - cycle.PeakTime;
        return cycle.Up - 0.5f * SwashSlowing * early * early;
    }
} // namespace

Wotah::SeaWaves Wotah::MakeSeaWaves(float significantHeight, const glm::vec2& towards)
{
    const float length = glm::length(towards);
    if (!(significantHeight > 0.0f) || !std::isfinite(significantHeight) || !(length > 0.0f) || !std::isfinite(length))
    {
        return {};
    }
    const glm::vec2 way = towards / length;

    // The swell: most of the height, longer the higher it is.
    SeaWaves waves;
    waves.Swell.Direction = way;
    waves.Swell.Height    = SwellShare * significantHeight;
    waves.Swell.Period    = std::max(MinSwellPeriod, PeriodPerRootHeight * std::sqrt(significantHeight));

    // The wind waves: the rest, around the wind sea's own peak (its period for its height, as the swell's).
    const float windHeight = WindShare * significantHeight;
    const float windPeriod = PeriodPerRootHeight * std::sqrt(windHeight);
    const float peakLength = StandardGravity * windPeriod * windPeriod / TwoPi;
    float       squares    = 0.0f;
    for (const WaveForm& form : WaveForms)
    {
        const float wavelength = form.Share * peakLength;
        if (wavelength < MinWindWavelength)
        {
            continue;
        }
        const float turn = glm::radians(form.TurnDegrees);
        SeaWave&    wave = waves.Waves[waves.Count];
        wave.Direction   = {way.x * std::cos(turn) - way.y * std::sin(turn),
                            way.x * std::sin(turn) + way.y * std::cos(turn)};
        wave.Wavelength  = wavelength;
        wave.Amplitude   = form.Share; // scaled below
        wave.Phase       = std::fmod(static_cast<float>(waves.Count) * GoldenAngle, TwoPi);
        squares += form.Share * form.Share;
        ++waves.Count;
    }

    // The scale that gives the wind sea's height, and each wave as sharp as real water's circles allow, unless the
    // waves together would fold a crest over.
    const float scale = squares > 0.0f ? windHeight / (SignificantPerRootSquares * std::sqrt(squares)) : 0.0f;
    for (uint32_t index = 0; index < waves.Count; ++index)
    {
        SeaWave& wave     = waves.Waves[index];
        wave.Amplitude    = wave.Amplitude * scale;
        const float slope = GetWavenumber(wave) * wave.Amplitude;
        wave.Steepness    = std::min(1.0f, FoldLimit / (slope * static_cast<float>(waves.Count)));
    }
    return waves;
}

float Wotah::GetWavenumber(const SeaWave& wave)
{
    return TwoPi / wave.Wavelength;
}

float Wotah::GetWaveFrequency(const SeaWave& wave)
{
    return std::sqrt(StandardGravity * GetWavenumber(wave));
}

float Wotah::GetWavePhase(const SeaWave& wave, double seconds)
{
    // In double: after a day of seconds a float's frequency x time would be off by most of a turn.
    constexpr double FullTurn = 2.0 * std::numbers::pi;
    const double     phase =
        std::fmod(static_cast<double>(wave.Phase) - static_cast<double>(GetWaveFrequency(wave)) * seconds, FullTurn);
    return static_cast<float>(phase < 0.0 ? phase + FullTurn : phase);
}

float Wotah::GetWaveReach(const SeaWaves& waves)
{
    float reach = 0.0f;
    for (uint32_t index = 0; index < waves.Count; ++index)
    {
        reach += waves.Waves[index].Amplitude;
    }
    return reach;
}

float Wotah::GetShoalRate(const SeaWaves& waves)
{
    const float reach = GetWaveReach(waves);
    return reach > 0.0f ? BreakingDepthShare / (2.0f * reach) : 0.0f;
}

float Wotah::GetShoalShare(const Sea& sea, const glm::vec2& place)
{
    const float rate = GetShoalRate(sea.Waves);
    if (!sea.Floor || !(rate > 0.0f))
    {
        return 1.0f;
    }

    // The share of the waves' full height the depth lets stand; none on land.
    const float depth = sea.Level - GetTerrainHeight(*sea.Floor, place, WaveFloorDetail);
    return glm::smoothstep(0.0f, 1.0f, depth * rate);
}

glm::vec3 Wotah::GetWaveOffset(const Sea& sea, const glm::vec2& restPlace)
{
    return GetWaveState(sea, restPlace).Offset;
}

float Wotah::GetSwellClock(const SeaSwell& swell, double seconds)
{
    // In double, and wrapped at a whole set: the sets' slow change follows the phase over that many turns.
    const double setTurn = 2.0 * std::numbers::pi * static_cast<double>(SwellSetWaves);
    const double clock   = std::fmod(static_cast<double>(swell.Phase) +
                                         2.0 * std::numbers::pi / static_cast<double>(swell.Period) * seconds,
                                     setTurn);
    return static_cast<float>(clock < 0.0 ? clock + setTurn : clock);
}

glm::vec2 Wotah::GetSurfFrame(const Sea& sea, const glm::vec2& place)
{
    if (sea.Floor)
    {
        const CoastPlace coast = GetCoastPlace(*sea.Floor, place, WaveFloorDetail);
        return {coast.Bent.x, coast.Seaward};
    }
    // Without a floor: across and against the swell's way, so its phase grows "seaward" as with one.
    const glm::vec2 way = sea.Waves.Swell.Direction;
    return {way.x * place.y - way.y * place.x, -glm::dot(way, place)};
}

Wotah::SurfPoint Wotah::GetSurfPoint(const Sea& sea, float seaward, float along, float clock)
{
    SurfPoint       point;
    const SeaSwell& swell = sea.Waves.Swell;
    point.Depth           = GetSwellDepth(sea, seaward);
    if (!(swell.Height > 0.0f) || (sea.Floor && !(swell.Direction.x > 0.0f)))
    {
        return point;
    }

    // Where it stands: its crests crowd together and turn to the shore as it slows over the shallowing floor.
    const float frequency   = GetSwellFrequency(swell);
    const float deep        = GetDeepNumber(swell);
    const float alongNumber = GetAlongNumber(sea, deep);
    point.Phase             = GetPhaseAcross(sea, seaward, deep, alongNumber) - alongNumber * along + clock;
    const float set         = GetSetShare(point.Phase, along);
    if (!(point.Depth > 0.0f))
    {
        // On the beach: no wave, but the mean water raised by the broken waves (the swash runs from there).
        point.Broken = 1.0f;
        point.Setup  = SetupShare * swell.Height * set;
        return point;
    }

    // How it grows, and breaks once the depth cannot hold it; then a bore, its height shrinking with the depth.
    const float local         = GetLocalNumber(deep, point.Depth);
    const float unbroken      = swell.Height * set * GetShoaling(deep, local, point.Depth, frequency);
    const float breaking      = BreakingDepthShare * point.Depth;
    const float ratio         = unbroken / breaking;
    point.Broken              = glm::smoothstep(BreakStart, BreakEnd, ratio);
    point.Height              = glm::mix(unbroken, BrokenDepthShare * point.Depth, point.Broken);
    const float unbrokenShare = 1.0f - point.Broken;
    point.Peak                = 1.0f + PeakGrowth * glm::smoothstep(PeakStart, 1.0f, ratio) * unbrokenShare;
    point.Lean  = MaxLean * glm::smoothstep(LeanStart, 1.0f, ratio) * unbrokenShare + BoreLean * point.Broken;
    point.Setup = SetupShare * swell.Height * set * point.Broken;
    return point;
}

float Wotah::GetSurfWave(const SurfPoint& point)
{
    // Within the wave: 0 at a crest, rising seaward over its back to the trough, then up its front to the next crest.
    // The front takes less of the wave the more it leans; both halves are half cosines.
    const float turns = point.Phase / TwoPi;
    const float place = turns - std::floor(turns);
    const float front = 0.5f * (1.0f - point.Lean);
    const float back  = 1.0f - front;
    const float lift  = place < back ? 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * place / back))
                                     : 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * (place - back) / front));

    // Sharper crests and flatter troughs, kept around the mean water: lift^p averages about 1 / sqrt(pi (p + 1/4)).
    const float mean = 1.0f / std::sqrt(std::numbers::pi_v<float> * (point.Peak + 0.25f));
    return point.Height * (std::pow(lift, point.Peak) - mean);
}

Wotah::Swash Wotah::GetSwash(const Sea& sea, float along, float clock)
{
    Swash swash;
    swash.Front = -NoFloorDepth;
    if (!HasSurf(sea))
    {
        return swash;
    }
    const SwashCycle cycle = GetSwashCycle(sea, along, clock, 0);
    swash.Front            = cycle.Shore - GetSwashReach(cycle, cycle.Since);
    swash.Thinnest         = SheetThinnest;
    swash.Deeper           = SheetShare * cycle.RunUp * GetSheetFreshness(cycle);
    swash.Speed            = cycle.Since < 2.0f * cycle.PeakTime ? SwashSlowing * (cycle.PeakTime - cycle.Since) : 0.0f;
    return swash;
}

float Wotah::GetSurfaceHeight(const Sea& sea, const glm::vec2& place, float sand, float windLift)
{
    float surface = sea.Level + windLift;
    if (!(sea.Waves.Swell.Height > 0.0f))
    {
        return surface;
    }

    // The swell over the mean water.
    const glm::vec2 frame = GetSurfFrame(sea, place);
    const float     clock = GetSwellClock(sea.Waves.Swell, sea.Seconds);
    const SurfPoint point = GetSurfPoint(sea, frame.y, frame.x, clock);
    surface += point.Setup + GetSurfWave(point);
    if (!HasSurf(sea) || point.Depth > SwashDepth)
    {
        return surface;
    }

    // The swash: landward of its front the sand lies bare (the water just under it); seaward a sheet of water at least
    // covers the sand, deeper behind the front.
    const Swash swash = GetSwash(sea, frame.x, clock);
    if (frame.y < swash.Front)
    {
        return swash.Front - frame.y < FrontMargin ? sand + swash.Thinnest : std::min(surface, sand - SandCover);
    }
    const float behind = std::clamp((frame.y - swash.Front) / SheetRamp, 0.0f, 1.0f);
    return std::max(surface, sand + swash.Thinnest + swash.Deeper * std::sqrt(behind));
}

float Wotah::GetSandWetness(const Sea& sea, const glm::vec2& place, float& trace)
{
    trace = 0.0f;
    if (!HasSurf(sea))
    {
        return 0.0f;
    }
    const glm::vec2 frame = GetSurfFrame(sea, place);
    const float     clock = GetSwellClock(sea.Waves.Swell, sea.Seconds);

    // The last few waves' swash: when each last left this place, and where each stopped.
    float wetness = 0.0f;
    for (int waves = 0; waves < WetWaves; ++waves)
    {
        const SwashCycle cycle = GetSwashCycle(sea, frame.x, clock, waves);
        const float      up    = cycle.Shore - frame.y; // how far up the beach from the mean waterline
        if (up <= -cycle.Down)
        {
            return 1.0f; // under the water even when it draws back
        }
        if (up > cycle.Up + WetEdge)
        {
            continue; // this wave did not get here
        }

        // The front passes here on its way up and back, the same time before and after its highest point; just above
        // where it stopped, the water soaks in from its highest point on.
        const float half = std::sqrt(2.0f * std::max(cycle.Up - up, 0.0f) / SwashSlowing);
        if (cycle.Since < cycle.PeakTime - half)
        {
            continue; // not yet
        }
        if (up <= cycle.Up && cycle.Since <= cycle.PeakTime + half)
        {
            return 1.0f; // under the swash now
        }
        // The oldest wave counted fades out as it ages, so none drops out at once.
        const float counted =
            std::clamp(static_cast<float>(WetWaves) - cycle.Since / sea.Waves.Swell.Period, 0.0f, 1.0f);
        const float soaked = 1.0f - glm::smoothstep(cycle.Up, cycle.Up + WetEdge, up);
        wetness =
            std::max(wetness, counted * soaked * std::exp(-(cycle.Since - cycle.PeakTime - half) / DryingSeconds));

        // The foam left where it stopped.
        const float stopped = cycle.Since - cycle.PeakTime;
        trace               = std::max(trace, counted * std::exp(-stopped / TraceSeconds) *
                                                  (1.0f - glm::smoothstep(0.0f, TraceWidth, std::abs(up - cycle.Up))));
    }
    return wetness;
}

namespace
{
    /**
     * The water that moved to place by the wind waves: steps back to where it lies at rest, each step from the last
     * guess's offset (it converges while no crest folds over), and its state there.
     */
    WaveState FindWindWaves(const Wotah::Sea& sea, const glm::vec2& place)
    {
        glm::vec2 rest = place;
        for (int step = 0; step < RestPlaceSteps && sea.Waves.Count > 0; ++step)
        {
            const glm::vec3 offset = Wotah::GetWaveOffset(sea, rest);
            rest                   = place - glm::vec2(offset.x, offset.z);
        }
        return GetWaveState(sea, rest);
    }

    /**
     * How the swell's water moves along the surface at place (x, z), in metres per second: on with its waves (linear
     * waves: the wave's speed times its height over a depth's share), towards the shore under a crest and back under a
     * trough, less back where the waves have broken (UndertowShare); on the swash's sheet as its front runs, up the
     * beach and back down. Towards the shore means down the steepest fall of the swell frame's seaward distance (the
     * swell turns to the shore); without a floor, the swell's own way.
     */
    glm::vec2 GetSwellFlow(const Wotah::Sea& sea, const glm::vec2& place)
    {
        const Wotah::SeaSwell& swell = sea.Waves.Swell;
        if (!(swell.Height > 0.0f))
        {
            return glm::vec2(0.0f);
        }
        const glm::vec2 frame     = Wotah::GetSurfFrame(sea, place);
        const glm::vec2 across    = {Wotah::GetSurfFrame(sea, place + glm::vec2(ShoreReach, 0.0f)).y -
                                         Wotah::GetSurfFrame(sea, place - glm::vec2(ShoreReach, 0.0f)).y,
                                     Wotah::GetSurfFrame(sea, place + glm::vec2(0.0f, ShoreReach)).y -
                                         Wotah::GetSurfFrame(sea, place - glm::vec2(0.0f, ShoreReach)).y};
        const glm::vec2 shoreward = glm::dot(across, across) > 0.0f ? -glm::normalize(across) : swell.Direction;

        const float            clock = Wotah::GetSwellClock(swell, sea.Seconds);
        const Wotah::SurfPoint point = Wotah::GetSurfPoint(sea, frame.y, frame.x, clock);
        const float            wave  = Wotah::GetSurfWave(point);
        float                  speed = 0.0f;
        if (point.Depth > 0.0f)
        {
            const float local = GetLocalNumber(GetDeepNumber(swell), point.Depth);
            speed             = GetSwellFrequency(swell) * wave / std::tanh(local * point.Depth);
            speed *= speed < 0.0f ? 1.0f - UndertowShare * point.Broken : 1.0f;

            const float turns  = point.Phase / TwoPi;
            const float within = turns - std::floor(turns);
            const float back   = 1.0f - 0.5f * (1.0f - point.Lean);
            const float roller = std::max(glm::smoothstep(back, 1.0f, within), std::exp(-within / RollerTrail));
            speed              = std::max(speed, point.Broken * roller * GetSwellFrequency(swell) / local);
            speed              = std::clamp(speed, -MaxSurfSpeed, MaxSurfSpeed);
        }

        // On the swash's sheet the water runs as its front does.
        if (HasSurf(sea) && point.Depth <= SwashDepth)
        {
            const Wotah::Swash swash = Wotah::GetSwash(sea, frame.x, clock);
            const float        sand  = Wotah::GetTerrainHeight(*sea.Floor, place);
            const float sheet = sand + swash.Thinnest +
                                swash.Deeper * std::sqrt(std::clamp((frame.y - swash.Front) / SheetRamp, 0.0f, 1.0f));
            if (frame.y >= swash.Front && sheet > sea.Level + point.Setup + wave)
            {
                speed = std::clamp(swash.Speed, -MaxSurfSpeed, MaxSurfSpeed);
            }
        }
        return shoreward * speed;
    }

    /** The swell's part of the surface at place (its surf and swash over the sand there), over the still level. */
    float GetSwellLift(const Wotah::Sea& sea, const glm::vec2& place)
    {
        const float sand = sea.Floor ? Wotah::GetTerrainHeight(*sea.Floor, place) : -NoFloorDepth;
        return Wotah::GetSurfaceHeight(sea, place, sand, 0.0f) - sea.Level;
    }
} // namespace

std::optional<Wotah::WaterSample> Wotah::SampleWater(const Sea& sea, float x, float z)
{
    if (!std::isfinite(x) || !std::isfinite(z))
    {
        return std::nullopt;
    }
    const glm::vec2 place{x, z};
    const WaveState wind = FindWindWaves(sea, place);

    // The swell's part, and its slope a little to each side.
    const auto swellAt = [](const glm::vec2& at, const Sea& when)
    {
        return GetSwellLift(when, at);
    };
    const float     swell = swellAt(place, sea);
    const glm::vec2 slope{
        (swellAt(place + glm::vec2(SlopeReach, 0.0f), sea) - swellAt(place - glm::vec2(SlopeReach, 0.0f), sea)) /
            (2.0f * SlopeReach),
        (swellAt(place + glm::vec2(0.0f, SlopeReach), sea) - swellAt(place - glm::vec2(0.0f, SlopeReach), sea)) /
            (2.0f * SlopeReach)};

    WaterSample sample;
    sample.Height             = sea.Level + wind.Offset.y + swell;
    const glm::vec2 windSlope = -glm::vec2(wind.Normal.x, wind.Normal.z) / wind.Normal.y;
    const glm::vec2 total     = windSlope + slope;
    sample.Normal             = glm::normalize(glm::vec3(-total.x, 1.0f, -total.y));

    // The swell's water moves up and down as the surface does, and on with its waves.
    Sea before = sea;
    Sea after  = sea;
    before.Seconds -= SpeedReach;
    after.Seconds += SpeedReach;
    const float     rising  = (swellAt(place, after) - swellAt(place, before)) / static_cast<float>(2.0 * SpeedReach);
    const glm::vec2 onwards = GetSwellFlow(sea, place);
    sample.Velocity         = wind.Velocity + glm::vec3(onwards.x, rising, onwards.y);
    return sample;
}

std::optional<float> Wotah::FindWaterHeight(const Sea& sea, float x, float z)
{
    if (!std::isfinite(x) || !std::isfinite(z))
    {
        return std::nullopt;
    }
    const glm::vec2 place{x, z};
    return sea.Level + FindWindWaves(sea, place).Offset.y + GetSwellLift(sea, place);
}
