/**
 * @file sky_lights.cpp
 * The sky's lights: sun and moon where the sky's clock has them, their light through the air, the eye getting used to
 * the dark, the sky's light, the ground's bounce and the haze, all following the sun's height.
 */
#include "render/sky_lights.hpp"

#include "render/sky_clock.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    /**
     * The sky's light, the blue sky lighting every surface a little, also those facing away from sun and moon: this
     * much with the sun at ReferenceSunElevation, less when it stands lower.
     */
    constexpr glm::vec3 SkyAmbient{0.2f, 0.24f, 0.32f};

    /**
     * The sun: warm white. Its color and the sky's light are their look with the sun this high; higher it turns
     * whiter and brighter, lower dimmer and redder.
     */
    constexpr float     ReferenceSunElevation = glm::radians(45.0f);
    constexpr glm::vec3 SunColor{1.0f, 0.95f, 0.85f};

    /**
     * How much of each color the grass throws back up: its color as drawn (shaders/terrain.frag, GrassColor), so the
     * light bounced onto things matches the ground they stand on.
     */
    constexpr glm::vec3 GrassReflectance{0.30f, 0.45f, 0.20f};

    /**
     * The least share of its start light the sky gives while the sun is up: around sunset the whole sky stays bright
     * after the sun's own light has faded. A guess, by eye.
     */
    constexpr float TwilightSkyShare = 0.5f;

    /** The share of its start light the sky gives at night: a dark-adapted eye still makes out the ground. By eye. */
    constexpr float NightSkyShare = 0.2f;

    /**
     * The air's haze, the color far ground fades into: by day the day sky's horizon (shaders/atmosphere.glsl,
     * HorizonColor), at sunset between the sunset band's orange and mauve, at night the night sky's horizon
     * (shaders/sky.frag: SunsetNearColor, SunsetFarColor, NightHorizonColor), so the ground meets the sky.
     */
    constexpr glm::vec3 DayHazeColor{0.62f, 0.78f, 0.94f};
    constexpr glm::vec3 DuskHazeColor{0.55f, 0.37f, 0.42f};
    constexpr glm::vec3 NightHazeColor{0.020f, 0.028f, 0.055f};

    /**
     * The full moon's light: cool blue, and faint. Real moonlight is a few millionths of sunlight (about 0.3 lux
     * against about 100 000 lux), but an eye used to the dark sees it as far more; this is how a night scene looks, by
     * eye. It counts only as night comes, and it casts its own shadows.
     */
    constexpr glm::vec3 MoonColor{0.45f, 0.55f, 1.0f};
    constexpr float     MoonIntensity = 0.45f;

    /** The full moon's disk high in the sky: white, a touch warm (its rock is gray-brown). */
    constexpr glm::vec3 MoonDiskColor{1.0f, 0.98f, 0.94f};

    /** Below this a color channel counts as none. */
    constexpr float NoLight = 1.0e-6f;

    /** How much each color counts towards how bright a color looks (luminance weights, ITU-R BT.709): green most. */
    constexpr glm::vec3 BrightnessWeights{0.2126f, 0.7152f, 0.0722f};

    /** How bright a color looks to the eye (its luminance): 1 for white at full strength. */
    float GetBrightness(const glm::vec3& color)
    {
        return glm::dot(color, BrightnessWeights);
    }

    /**
     * The color of a light from elevation after the air it crosses, brightest channel 1: white overhead, warm low
     * down, orange-red on the horizon (and below it, as it was there), as a rising moon looks.
     */
    glm::vec3 MakeAirTint(float elevation)
    {
        const glm::vec3 throughAir = Wotah::MakeSunlightThroughAir(std::max(elevation, 0.0f));
        const float     brightest  = std::max({throughAir.r, throughAir.g, throughAir.b});
        return brightest > NoLight ? throughAir / brightest : glm::vec3(1.0f, 0.0f, 0.0f);
    }
} // namespace

Wotah::SkyLights Wotah::MakeSkyLights(const SkyLightSettings& settings)
{
    // Where sun and moon stand at this time of day (render/sky_clock.hpp), and how far night has come.
    const glm::vec3 sunDirection  = MakeSunDirection(settings.TimeOfDayHours, settings.NoonElevation);
    const glm::vec3 moonDirection = MakeMoonDirection(settings.TimeOfDayHours, settings.NoonElevation);
    const float     sunElevation  = GetElevation(sunDirection);
    const float     moonElevation = GetElevation(moonDirection);
    const float     night         = GetNightShare(sunElevation);

    // The sun's light: the lower the sun, the more air it crosses, so it dims and reddens (MakeSunlightThroughAir),
    // measured against ReferenceSunElevation. Below the horizon it stays the horizon's light; how much of it is seen
    // or reaches the scene is decided below.
    const glm::vec3 throughAir =
        MakeSunlightThroughAir(std::max(sunElevation, 0.0f)) / MakeSunlightThroughAir(ReferenceSunElevation);

    // The eye gets used to less light and makes up for part of the loss: it sees the light's drop in brightness as its
    // square root (a guess, by eye). So a sun on the horizon, which keeps about a twentieth of the start's brightness,
    // looks about a quarter as bright: dim and red, not black. The color keeps the air's reddening in full.
    const float     dimming  = GetBrightness(throughAir);
    const float     seen     = std::sqrt(dimming);
    const glm::vec3 sunColor = dimming > 0.0f ? SunColor * throughAir * (seen / dimming) : glm::vec3(0.0f);

    // On the scene only the part of the disk above the horizon shines; the sky's disk stays whole until it has sunk
    // behind the ground.
    SkyLights lights;
    lights.Sun.Direction     = sunDirection;
    lights.Sun.Color         = sunColor * GetHorizonShare(sunElevation);
    lights.Sun.Intensity     = settings.SunBrightness;
    lights.SunDisk.Direction = sunDirection;
    lights.SunDisk.Color     = sunColor * GetDiskShowing(sunElevation);
    lights.SunDisk.Intensity = settings.SunBrightness;

    // The moon's light on the scene counts as night comes, from above the horizon; its disk shows whenever it is up,
    // tinted by the air.
    lights.Moon.Direction     = moonDirection;
    lights.Moon.Color         = MoonColor;
    lights.Moon.Intensity     = MoonIntensity * settings.MoonBrightness * night * GetHorizonShare(moonElevation);
    lights.MoonDisk.Direction = moonDirection;
    lights.MoonDisk.Color     = MoonDiskColor * MakeAirTint(moonElevation);
    lights.MoonDisk.Intensity = settings.MoonBrightness * GetDiskShowing(moonElevation);

    // The sky fades with the sun as the eye sees it, but keeps at least its twilight share: the whole sky stays bright
    // for a while around sunset, after the sun's own light has gone. Then night comes and it fades to its night share.
    const float skyShare = glm::mix(std::max(seen, TwilightSkyShare), NightSkyShare, night);
    lights.AmbientColor  = SkyAmbient * settings.SkyLight * skyShare;

    // Far ground fades into the haze, which takes the sky's colors at the horizon: day, sunset, night.
    const glm::vec3 evening = glm::mix(DayHazeColor, DuskHazeColor, GetSunsetShare(sunElevation));
    lights.HazeColor        = glm::mix(evening, NightHazeColor, night);

    // The grass throws part of the light falling on it back up: the sun's and the moon's (as squarely as they fall on
    // flat ground) and the sky's. Upright sides get half of it and undersides all, so they are not lit by the sky
    // alone; most of all with the sun high, as at noon, when it shines past upright sides and they would look nearly
    // black.
    const glm::vec3 sunOnGround  = lights.Sun.Color * settings.SunBrightness * std::max(std::sin(sunElevation), 0.0f);
    const glm::vec3 moonOnGround = MoonColor * lights.Moon.Intensity * std::max(std::sin(moonElevation), 0.0f);
    lights.GroundBounce          = GrassReflectance * (sunOnGround + moonOnGround + lights.AmbientColor);
    return lights;
}
