/**
 * @file sea_tests.cpp
 * The sea (water/sea.hpp): still water at its level; the waves MakeSeaWaves makes (the swell's height and period, the
 * wind waves' height, steepness, never folding over); their clocks; the wind waves' surface (its height where the
 * water moved to, its normal, the water's speed, all against finite differences) and how they die out over the shallow
 * floor; the swell's surf (growing, sharpening, leaning and breaking as the floor shallows, its crests crowding and
 * slowing towards the shore); its swash (running up the beach, drawing back below the waterline, the sand bare behind
 * it); and the wet sand it leaves. No GPU needed; tests/gpu_tests.cpp compares the shaders' twin.
 */
#include "test_utils.hpp"
#include "water/sea.hpp"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace
{
    /** The beach's land, as the sea's floor. */
    const Wotah::TerrainShape Beach{};

    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

    /** A sea of waves significantHeight high rolling east, at second 100, over the beach's floor or none. */
    Wotah::Sea MakeWavySea(float significantHeight, bool hasFloor)
    {
        Wotah::Sea sea;
        sea.Level   = Beach.SeaLevel;
        sea.Waves   = Wotah::MakeSeaWaves(significantHeight, {1.0f, 0.0f});
        sea.Seconds = 100.0;
        if (hasFloor)
        {
            sea.Floor = Beach;
        }
        return sea;
    }

    /** The same sea with the wind's waves only, no swell. */
    Wotah::Sea MakeWindSea(float significantHeight, bool hasFloor)
    {
        Wotah::Sea sea         = MakeWavySea(significantHeight, hasFloor);
        sea.Waves.Swell.Height = 0.0f;
        return sea;
    }

    /** How deep the beach's sea is at place (below 0 on land), as the wind waves feel it. */
    float GetDepth(const Wotah::Sea& sea, const glm::vec2& place)
    {
        return sea.Level - Wotah::GetTerrainHeight(Beach, place, Wotah::WaveFloorDetail);
    }

    /** The surface's height at x, z; a test of a place that is a number always has one. */
    float GetHeight(const Wotah::Sea& sea, float x, float z)
    {
        return Wotah::FindWaterHeight(sea, x, z).value_or(std::numeric_limits<float>::quiet_NaN());
    }

    /** The wind waves' significant height from their amplitudes: 2 sqrt 2 sqrt(sum a^2). */
    float GetWindHeight(const Wotah::SeaWaves& waves)
    {
        float squares = 0.0f;
        for (uint32_t index = 0; index < waves.Count; ++index)
        {
            squares += waves.Waves[index].Amplitude * waves.Waves[index].Amplitude;
        }
        return 2.0f * std::numbers::sqrt2_v<float> * std::sqrt(squares);
    }
} // namespace

TEST_SUITE("sea")
{
    TEST_CASE(
        "the still sea's surface lies at its level everywhere, near and far, and nowhere for a place that is no number")
    {
        const Wotah::Sea sea = Wotah::Testing::MakeStillSea(-1.5f);
        CHECK(Wotah::FindWaterHeight(sea, 0.0f, 0.0f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(sea, 31.9f, -15.9f) == -1.5f);
        CHECK(Wotah::FindWaterHeight(sea, -5000.0f, 12000.0f) == -1.5f);
        const std::optional<Wotah::WaterSample> still = Wotah::SampleWater(sea, 3.0f, 4.0f);
        REQUIRE(still.has_value());
        CHECK(still->Normal == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(still->Velocity == glm::vec3(0.0f));

        constexpr float NoNumber = std::numeric_limits<float>::quiet_NaN();
        constexpr float Endless  = std::numeric_limits<float>::infinity();
        CHECK_FALSE(Wotah::FindWaterHeight(sea, NoNumber, 0.0f).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(sea, 0.0f, NoNumber).has_value());
        CHECK_FALSE(Wotah::FindWaterHeight(sea, Endless, 0.0f).has_value());
        CHECK_FALSE(Wotah::SampleWater(sea, NoNumber, 0.0f).has_value());

        // No swell, no swash, no wet sand.
        float trace = 1.0f;
        CHECK(Wotah::GetSandWetness(sea, {0.0f, 0.0f}, trace) == 0.0f);
        CHECK(trace == 0.0f);
        CHECK(Wotah::GetSwash(sea, 0.0f, 0.0f).Front < -1000.0f);
    }

    TEST_CASE("made waves: mostly swell, longer the higher; wind waves around it, steep but never folding; none else")
    {
        float lastPeriod     = 0.0f;
        float lastWindLength = 0.0f;
        for (const float height : {0.1f, 0.5f, 1.5f, 3.0f})
        {
            CAPTURE(height);
            const Wotah::SeaWaves waves = Wotah::MakeSeaWaves(height, {3.0f, 4.0f});

            // The swell: 0.85 of the height, rolling the asked way, at least 7 s apart, 4.99 sqrt(height) for high
            // ones.
            CHECK(std::abs(waves.Swell.Height - 0.85f * height) < 1e-5f);
            CHECK(glm::distance(waves.Swell.Direction, glm::vec2(0.6f, 0.8f)) < 1e-5f);
            CHECK(std::abs(waves.Swell.Period - std::max(7.0f, 4.99f * std::sqrt(height))) < 1e-4f);
            CHECK(waves.Swell.Period >= lastPeriod);
            lastPeriod = waves.Swell.Period;

            // The wind waves: 0.53 of the height (their energies add up with the swell's), none shorter than 2 m,
            // longer the higher, each as sharp as it may be without the crests together folding over.
            REQUIRE(waves.Count > 0);
            REQUIRE(waves.Count <= Wotah::MaxSeaWaves);
            CHECK(std::abs(GetWindHeight(waves) - 0.53f * height) < 1e-4f * height + 1e-6f);
            float folding = 0.0f;
            for (uint32_t index = 0; index < waves.Count; ++index)
            {
                const Wotah::SeaWave& wave = waves.Waves[index];
                folding += wave.Steepness * Wotah::GetWavenumber(wave) * wave.Amplitude;
                CHECK(std::abs(glm::length(wave.Direction) - 1.0f) < 1e-5f);
                CHECK(wave.Steepness > 0.0f);
                CHECK(wave.Steepness <= 1.0f);
                CHECK(wave.Wavelength >= 2.0f); // the nearest drawn water still shows it
            }
            CHECK(folding < 1.0f);
            CHECK(waves.Waves[0].Wavelength > lastWindLength);
            lastWindLength = waves.Waves[0].Wavelength;
            CHECK(glm::distance(waves.Waves[0].Direction, glm::vec2(0.6f, 0.8f)) < 1e-5f);
        }

        // 3 m seas: a swell over 100 m long in deep water (g T^2 / 2 pi), wind waves of more than 50 m.
        const Wotah::SeaWaves rough = Wotah::MakeSeaWaves(3.0f, {1.0f, 0.0f});
        CHECK(9.80665f * rough.Swell.Period * rough.Swell.Period / TwoPi > 100.0f);
        CHECK(rough.Waves[0].Wavelength > 50.0f);

        for (const float nonsense : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            const Wotah::SeaWaves none = Wotah::MakeSeaWaves(nonsense, {1.0f, 0.0f});
            CHECK(none.Count == 0);
            CHECK(none.Swell.Height == 0.0f);
        }
        CHECK(Wotah::MakeSeaWaves(1.0f, {0.0f, 0.0f}).Count == 0);
        CHECK(Wotah::MakeSeaWaves(1.0f, {std::numeric_limits<float>::infinity(), 0.0f}).Count == 0);
        CHECK(Wotah::GetShoalRate(Wotah::SeaWaves{}) == 0.0f);
    }

    TEST_CASE("the clocks: a wind wave comes round after its period, the swell after a whole set, even a day later")
    {
        Wotah::SeaWave wave;
        wave.Wavelength = 42.0f;
        wave.Phase      = 1.0f;
        CHECK(Wotah::GetWavenumber(wave) == doctest::Approx(2.0 * std::numbers::pi / 42.0));
        CHECK(Wotah::GetWaveFrequency(wave) == doctest::Approx(std::sqrt(9.80665 * 2.0 * std::numbers::pi / 42.0)));
        const double period = 2.0 * std::numbers::pi / static_cast<double>(Wotah::GetWaveFrequency(wave));
        for (const double start : {0.0, 12.3, 86400.0, 1.0e6})
        {
            CAPTURE(start);
            const float now   = Wotah::GetWavePhase(wave, start);
            const float later = Wotah::GetWavePhase(wave, start + period);
            CHECK(now >= 0.0f);
            CHECK(now < TwoPi);
            const float apart = std::abs(now - later);
            CHECK(std::min(apart, TwoPi - apart) < 1e-4f);
        }
        CHECK(std::abs(Wotah::GetWavePhase(wave, 0.0) - 1.0f) < 1e-6f);

        // The swell's clock grows with time (its crests come shoreward) and wraps after a whole set of waves.
        Wotah::SeaSwell swell;
        swell.Period    = 8.0f;
        swell.Phase     = 0.5f;
        const float set = TwoPi * Wotah::SwellSetWaves;
        CHECK(std::abs(Wotah::GetSwellClock(swell, 0.0) - 0.5f) < 1e-6f);
        CHECK(std::abs(Wotah::GetSwellClock(swell, 2.0) - (0.5f + TwoPi / 4.0f)) < 1e-5f);
        for (const double start : {3.0, 86400.0, 1.0e6})
        {
            CAPTURE(start);
            const float now   = Wotah::GetSwellClock(swell, start);
            const float later = Wotah::GetSwellClock(swell, start + 8.0 * static_cast<double>(Wotah::SwellSetWaves));
            CHECK(now >= 0.0f);
            CHECK(now < set);
            const float apart = std::abs(now - later);
            CHECK(std::min(apart, set - apart) < 1e-3f);
        }
    }

    TEST_CASE("the waves' surface: around the level, moving over time, its height found where the water moved to")
    {
        const Wotah::Sea sea = MakeWavySea(1.0f, false);

        // Averaged over a wide square the surface lies at the level, never farther off than the waves reach.
        const float reach  = Wotah::GetWaveReach(sea.Waves) + sea.Waves.Swell.Height;
        double      sum    = 0.0;
        int         count  = 0;
        float       lowest = 1.0e9f;
        float       top    = -1.0e9f;
        for (float x = -200.0f; x <= 200.0f; x += 1.7f)
        {
            for (float z = -200.0f; z <= 200.0f; z += 1.9f)
            {
                const float height = GetHeight(sea, x, z);
                sum += static_cast<double>(height);
                ++count;
                lowest = std::min(lowest, height);
                top    = std::max(top, height);
            }
        }
        CHECK(std::abs(sum / count - static_cast<double>(sea.Level)) < 0.03);
        CHECK(top <= sea.Level + reach);
        CHECK(lowest >= sea.Level - reach);
        CHECK(top - lowest > 0.8f); // real waves

        // A second later the surface at a place is another.
        Wotah::Sea later = sea;
        later.Seconds += 1.0;
        CHECK(std::abs(GetHeight(later, 3.0f, 4.0f) - GetHeight(sea, 3.0f, 4.0f)) > 1e-3f);

        // The water at rest at a place moved by the wind waves' offset; the height found where it moved to is its
        // height plus the swell's there.
        for (const float height : {0.5f, 3.0f})
        {
            CAPTURE(height);
            const Wotah::Sea wavy    = MakeWavySea(height, false);
            float            largest = 0.0f;
            for (float x = -40.0f; x <= 40.0f; x += 3.1f)
            {
                for (float z = -40.0f; z <= 40.0f; z += 2.9f)
                {
                    const glm::vec3 offset = Wotah::GetWaveOffset(wavy, {x, z});
                    const glm::vec2 moved{x + offset.x, z + offset.z};
                    const float     expected = Wotah::GetSurfaceHeight(wavy, moved, -1.0e4f, offset.y);
                    largest = std::max(largest, std::abs(GetHeight(wavy, moved.x, moved.y) - expected));
                }
            }
            CHECK(largest < 2e-3f * height);
        }
    }

    TEST_CASE("the wind waves' normal and the water's speed match the surface's slope and the water's motion")
    {
        const Wotah::Sea sea = MakeWindSea(1.0f, false);

        // The normal against the slope of the heights a little to each side; the speed against where the water at
        // rest at a place is a moment before and after.
        constexpr float  Aside  = 0.02f;
        constexpr double Moment = 1.0e-3;
        for (const glm::vec2 place : {glm::vec2(0.0f), glm::vec2(7.3f, -2.1f), glm::vec2(-31.0f, 18.5f)})
        {
            CAPTURE(place.x);
            CAPTURE(place.y);
            const std::optional<Wotah::WaterSample> sample = Wotah::SampleWater(sea, place.x, place.y);
            REQUIRE(sample.has_value());
            const float slopeX =
                (GetHeight(sea, place.x + Aside, place.y) - GetHeight(sea, place.x - Aside, place.y)) / (2.0f * Aside);
            const float slopeZ =
                (GetHeight(sea, place.x, place.y + Aside) - GetHeight(sea, place.x, place.y - Aside)) / (2.0f * Aside);
            CHECK(std::abs(-sample->Normal.x / sample->Normal.y - slopeX) < 0.01f);
            CHECK(std::abs(-sample->Normal.z / sample->Normal.y - slopeZ) < 0.01f);
            CHECK(std::abs(glm::length(sample->Normal) - 1.0f) < 1e-5f);

            // The water at rest at place: how fast it moves, and its speed as SampleWater gives it where it is now.
            Wotah::Sea before = sea;
            Wotah::Sea after  = sea;
            before.Seconds -= Moment;
            after.Seconds += Moment;
            const glm::vec3 moved  = (Wotah::GetWaveOffset(after, place) - Wotah::GetWaveOffset(before, place)) /
                                     static_cast<float>(2.0 * Moment);
            const glm::vec3 offset = Wotah::GetWaveOffset(sea, place);
            const std::optional<Wotah::WaterSample> there =
                Wotah::SampleWater(sea, place.x + offset.x, place.y + offset.z);
            REQUIRE(there.has_value());
            CHECK(glm::distance(there->Velocity, moved) < 0.01f);
        }

        // With the swell too, the normal still follows the whole surface's slope.
        const Wotah::Sea                        swell  = MakeWavySea(1.0f, false);
        const std::optional<Wotah::WaterSample> sample = Wotah::SampleWater(swell, 5.0f, 1.0f);
        REQUIRE(sample.has_value());
        const float slopeX =
            (GetHeight(swell, 5.0f + Aside, 1.0f) - GetHeight(swell, 5.0f - Aside, 1.0f)) / (2.0f * Aside);
        CHECK(std::abs(-sample->Normal.x / sample->Normal.y - slopeX) < 0.02f);
    }

    TEST_CASE("over the shallow floor the wind waves die out as the depth lets them stand, none at the shore")
    {
        const Wotah::Sea sea = MakeWindSea(1.0f, true);
        CHECK(Wotah::GetShoalShare(MakeWindSea(1.0f, false), {0.0f, 0.0f}) == 1.0f);
        Wotah::Sea still = sea;
        still.Waves      = {};
        CHECK(Wotah::GetShoalShare(still, {0.0f, 0.0f}) == 1.0f);

        // Walking out from the dry beach along z = 0: none on land and at the waterline, more the deeper (a deeper
        // place never has less), all where the water is deep enough for the highest crest.
        const float                          full = 2.0f * Wotah::GetWaveReach(sea.Waves) / Wotah::BreakingDepthShare;
        std::vector<std::pair<float, float>> byDepth;
        for (float x = 30.0f; x >= -300.0f; x -= 1.0f)
        {
            CAPTURE(x);
            const float depth = GetDepth(sea, {x, 0.0f});
            const float share = Wotah::GetShoalShare(sea, {x, 0.0f});
            if (depth <= 0.0f)
            {
                CHECK(share == 0.0f);
            }
            if (depth >= full)
            {
                CHECK(std::abs(share - 1.0f) < 1e-5f);
            }
            byDepth.emplace_back(depth, share);
        }
        std::ranges::sort(byDepth);
        CHECK(byDepth.front().first <= 0.0f);
        CHECK(byDepth.back().first >= full);
        for (std::size_t index = 1; index < byDepth.size(); ++index)
        {
            CHECK(byDepth[index].second >= byDepth[index - 1].second - 1e-6f);
        }
    }

    TEST_CASE("the swell rolling in: it grows, sharpens and leans as the floor shallows, breaks, and runs on as a bore")
    {
        const Wotah::Sea sea   = MakeWavySea(1.5f, true);
        const float      clock = Wotah::GetSwellClock(sea.Waves.Swell, sea.Seconds);
        const float      along = Wotah::GetSurfFrame(sea, {0.0f, 0.0f}).x;

        // Walking in from 400 m out to the waterline: each point as the swell stands there, its phase held at a crest
        // (so the sets, which follow the phase, do not change its size on the way).
        float              deepHeight = 0.0f;
        float              tallest    = 0.0f;
        bool               isBroken   = false;
        Wotah::SurfPoint   last;
        std::vector<float> phases;
        for (float seaward = 400.0f; seaward > 0.05f; seaward *= 0.97f)
        {
            CAPTURE(seaward);
            phases.push_back(Wotah::GetSurfPoint(sea, seaward, along, clock).Phase);
            const float            across = Wotah::GetSurfPoint(sea, seaward, along, 0.0f).Phase;
            const Wotah::SurfPoint point  = Wotah::GetSurfPoint(sea, seaward, along, -across);
            if (deepHeight == 0.0f)
            {
                deepHeight = point.Height;
            }
            CHECK(point.Depth > 0.0f);
            CHECK(point.Height <= Wotah::BreakingDepthShare * 1.3f * point.Depth + 1e-4f); // never far over breaking
            if (point.Broken < 0.01f)
            {
                // Unbroken: it grows over the shallowing floor (shoaling), its crests sharpen and its front leans.
                tallest = std::max(tallest, point.Height);
                CHECK(point.Peak >= last.Peak - 1e-4f);
                CHECK(point.Lean >= last.Lean - 1e-4f);
                CHECK(!isBroken); // once broken, it stays broken on the way in
            }
            else
            {
                isBroken = isBroken || point.Broken > 0.99f;
            }
            if (point.Broken > 0.99f)
            {
                // A bore: half the depth high, its front a near-wall, the mean water raised behind the break.
                CHECK(std::abs(point.Height - 0.5f * point.Depth) < 1e-4f);
                CHECK(point.Lean > 0.85f);
                CHECK(point.Setup > 0.0f);
            }
            last = point;
        }
        CHECK(isBroken);
        CHECK(tallest > 1.15f * deepHeight); // it grew before it broke
        CHECK(last.Height < 0.05f);          // nearly nothing left at the waterline

        // Its phase grows seaward, ever faster near the shore (the crests crowd together where the water is shallow).
        for (std::size_t index = 1; index < phases.size(); ++index)
        {
            CHECK(phases[index] < phases[index - 1]);
        }
        const float nearShore =
            (Wotah::GetSurfPoint(sea, 2.0f, along, clock).Phase - Wotah::GetSurfPoint(sea, 1.0f, along, clock).Phase);
        const float farOut = (Wotah::GetSurfPoint(sea, 301.0f, along, clock).Phase -
                              Wotah::GetSurfPoint(sea, 300.0f, along, clock).Phase);
        CHECK(nearShore > 2.0f * farOut);

        // A crest moves shoreward with time, slower near the shore.
        const auto crestSpeed = [&sea, along](float seaward)
        {
            const float dPhaseDx = (Wotah::GetSurfPoint(sea, seaward + 0.5f, along, 0.0f).Phase -
                                    Wotah::GetSurfPoint(sea, seaward - 0.5f, along, 0.0f).Phase);
            return TwoPi / sea.Waves.Swell.Period / dPhaseDx;
        };
        CHECK(crestSpeed(200.0f) > 2.0f * crestSpeed(3.0f));

        // Without a floor the swell is a plain round wave, half its height up and down, nothing breaks.
        const Wotah::Sea open = MakeWavySea(1.5f, false);
        float            high = -1.0f;
        float            low  = 1.0f;
        for (float phase = 0.0f; phase < TwoPi; phase += 0.01f)
        {
            Wotah::SurfPoint point = Wotah::GetSurfPoint(open, 10.0f, 0.0f, 0.0f);
            CHECK(point.Broken == 0.0f);
            CHECK(point.Peak == 1.0f);
            CHECK(point.Lean == 0.0f);
            point.Phase      = phase;
            const float wave = Wotah::GetSurfWave(point);
            high             = std::max(high, wave);
            low              = std::min(low, wave);
        }
        CHECK(std::abs(high - open.Waves.Swell.Height / 2.0f) < 0.05f * open.Waves.Swell.Height);
        CHECK(std::abs(low + open.Waves.Swell.Height / 2.0f) < 0.05f * open.Waves.Swell.Height);
    }

    TEST_CASE("the swash: each wave runs up the sand and draws back below the waterline; behind it the sand lies bare")
    {
        for (const float height : {0.5f, 3.0f})
        {
            CAPTURE(height);
            Wotah::Sea      sea   = MakeWavySea(height, true);
            const glm::vec2 place = {0.0f, 0.0f};
            const float     along = Wotah::GetSurfFrame(sea, place).x;

            // Over a whole set of waves: the front runs up the beach (negative: landward of the still waterline) and
            // back seaward of where it started each time.
            float       landward = 1.0e9f;
            float       seaward  = -1.0e9f;
            const float set      = TwoPi * Wotah::SwellSetWaves;
            for (float clock = 0.01f; clock < set; clock += 0.01f)
            {
                const Wotah::Swash swash = Wotah::GetSwash(sea, along, clock);
                landward                 = std::min(landward, swash.Front);
                seaward                  = std::max(seaward, swash.Front);
                CHECK(swash.Thinnest > 0.0f);
                CHECK(swash.Deeper >= 0.0f);
            }
            CAPTURE(landward);
            CAPTURE(seaward);
            CHECK(landward < -1.0f);          // up the dry beach
            CHECK(seaward > landward + 2.0f); // and back

            // Where the front is: well landward of it the surface lies under the sand; just landward of it, the
            // thinnest sheet, which the water's shader cuts off at the front; seaward of it over the sand.
            sea.Seconds              = 123.4;
            const float        clock = Wotah::GetSwellClock(sea.Waves.Swell, sea.Seconds);
            const Wotah::Swash swash = Wotah::GetSwash(sea, along, clock);
            for (const float aside : {-3.0f, -1.0f, 1.5f})
            {
                // A place on the same line across the coast, aside metres seaward of the front.
                float x = 0.0f;
                for (int step = 0; step < 50; ++step)
                {
                    const float off = Wotah::GetSurfFrame(sea, {x, 0.0f}).y - (swash.Front + aside);
                    x += off; // seaward is -x here
                }
                const glm::vec2 there{x, 0.0f};
                const float     sand    = Wotah::GetTerrainHeight(Beach, there);
                const float     surface = Wotah::GetSurfaceHeight(sea, there, sand, 0.0f);
                CAPTURE(aside);
                if (aside < -2.0f)
                {
                    CHECK(surface < sand);
                }
                else if (aside < 0.0f)
                {
                    CHECK(std::abs(surface - sand - swash.Thinnest) < 1e-4f);
                }
                else
                {
                    CHECK(surface > sand);
                }
            }
        }
    }

    TEST_CASE("the swash runs on smoothly from wave to wave: frame by frame its front and sheet never jump")
    {
        // At 60 frames a second over two sets of waves, low and high seas: the front moves no farther in a frame than
        // its speed takes it (a little more for rounding), and the sheet's depth changes by less than 3 cm. Once a new
        // wave's swash started from where its own size put it and with its full sheet at once: the swash shook with
        // every wave.
        for (const float height : {0.5f, 1.0f, 3.0f})
        {
            CAPTURE(height);
            Wotah::Sea      sea    = MakeWavySea(height, true);
            const float     along  = Wotah::GetSurfFrame(sea, {0.0f, 0.0f}).x;
            constexpr float Frame  = 1.0f / 60.0f;
            float           jumped = 0.0f;
            float           rose   = 0.0f;
            Wotah::Swash    last   = Wotah::GetSwash(sea, along, Wotah::GetSwellClock(sea.Waves.Swell, sea.Seconds));
            for (int frame = 1; frame < static_cast<int>(2.0f * Wotah::SwellSetWaves * sea.Waves.Swell.Period / Frame);
                 ++frame)
            {
                sea.Seconds = 100.0 + static_cast<double>(frame) * static_cast<double>(Frame);
                const Wotah::Swash swash =
                    Wotah::GetSwash(sea, along, Wotah::GetSwellClock(sea.Waves.Swell, sea.Seconds));
                const float moved = std::abs(swash.Front - last.Front);
                jumped = std::max(jumped, moved - std::max(std::abs(swash.Speed), std::abs(last.Speed)) * Frame);
                rose   = std::max(rose, std::abs(swash.Deeper - last.Deeper));
                last   = swash;
            }
            CAPTURE(jumped);
            CAPTURE(rose);
            CHECK(jumped < 0.02f);
            CHECK(rose < 0.03f);
        }
    }

    TEST_CASE("wet sand: under the swash and just after it wet, drying over time; dry far up; foam where it stopped")
    {
        Wotah::Sea sea = MakeWavySea(1.0f, true);

        // Far up the beach it never gets wet.
        float trace = 0.0f;
        CHECK(Wotah::GetSandWetness(sea, {40.0f, 0.0f}, trace) == 0.0f);
        CHECK(trace == 0.0f);

        // Just up the beach from the waterline, over two minutes: covered now and then (wet 1), drying in between,
        // never fully dry; the foam line shows some of the time.
        const glm::vec2 place{1.5f, 0.0f};
        int             covered  = 0;
        float           driest   = 1.0f;
        float           last     = Wotah::GetSandWetness(sea, place, trace);
        bool            isDrying = true;
        for (double second = 0.0; second < 120.0; second += 0.25)
        {
            sea.Seconds     = 1000.0 + second;
            const float wet = Wotah::GetSandWetness(sea, place, trace);
            covered += wet == 1.0f ? 1 : 0;
            driest = std::min(driest, wet);
            CHECK(wet >= 0.0f);
            CHECK(wet <= 1.0f);
            if (wet < 1.0f && last < 1.0f && wet > last + 1e-4f)
            {
                // Between two covers it only dries; a rise means the swash's tip touched it between two looks.
                isDrying = isDrying && wet > 0.95f;
            }
            last = wet;
        }
        CAPTURE(covered);
        CAPTURE(driest);
        CHECK(covered > 4);
        CHECK(covered < 470);
        CHECK(driest > 0.5f);
        CHECK(isDrying);

        // Somewhere up the beach, soon after a run-up stopped, its foam line shows.
        float mostTrace = 0.0f;
        for (double second = 0.0; second < 20.0 && mostTrace < 0.5f; second += 0.5)
        {
            sea.Seconds = 2000.0 + second;
            for (float x = 0.0f; x < 12.0f; x += 0.05f)
            {
                (void)Wotah::GetSandWetness(sea, {x, 0.0f}, trace);
                mostTrace = std::max(mostTrace, trace);
            }
        }
        CHECK(mostTrace > 0.5f);
    }
}
