// The sky, shared by the sky itself (sky.frag) and what mirrors it (water.frag): include atmosphere.glsl and
// noise.glsl first, then this (#include "sky.glsl"). GetSkyBehindSun gives the sky with its stars and the moon,
// GetSunLook what the sun adds; AddLight puts the sun's light over the sky. Both take the camera's directions, so sun
// and moon stay round on the picture (for a mirror image, the mirrored camera's directions).

const float Pi           = 3.14159265;
const float HourSeconds  = 3600.0; // the sky's clock repeats hourly: every movement turns whole times an hour
const vec3  WorldUp      = vec3(0.0, 1.0, 0.0);
const float NoFlatPart   = 1.0e-4; // below this length a direction's flat part (x, z) counts as none: straight up/down
const float NoLight      = 1.0e-4; // below this a body gives no light (switched off): it is not drawn at all

// ---- The sky: day, sunset, night ----

const vec3  ZenithColor = vec3(0.16, 0.38, 0.78); // straight up by day: deep blue
const vec3  BelowColor  = vec3(0.30, 0.36, 0.42); // below the horizon: blue-gray haze, seen only from under the ground
const float SkyCurve    = 3.0;  // the blue deepens fast just above the horizon, then evens out towards the top
const float BelowFade   = 0.25; // the haze reaches its full color this far below the horizon (as the ray's y)

// The sunset: with the sun lower than SunsetHeight (as towardsSun.y; sin 20 degrees), a warm band lies along the
// horizon, orange on the sun's side and dusky mauve away from it, and the rest of the sky dims. By eye, after
// photographs of clear sunsets.
const vec3  SunsetNearColor = vec3(0.85, 0.40, 0.16); // the horizon towards the sun: orange
const vec3  SunsetFarColor  = vec3(0.45, 0.36, 0.50); // the horizon away from it: dusky mauve
const float SunsetHeight    = 0.34; // sin(20 degrees)
const float SunsetBand      = 5.0;  // how fast the band fades upwards from the horizon, per unit of the ray's y
const float SunsetFocus     = 4.0;  // how tightly the orange gathers on the sun's side of the sky
const float TwilightDimming = 0.5;  // how bright the rest of the sky stays with the sun on the horizon

// The night: as the sun sinks to NightDepth below the horizon (as -towardsSun.y; sin 12 degrees, the end of nautical
// twilight, when the sky is dark), the sunset fades into a near black sky, a little lighter towards the horizon.
const vec3  NightZenithColor  = vec3(0.004, 0.007, 0.018);
const vec3  NightHorizonColor = vec3(0.020, 0.028, 0.055);
const float NightDepth        = 0.21; // sin(12 degrees)

// ---- The stars ----

// The sky is cut into cells of 1 / StarCells of a direction (about 0.6 degrees); StarChance of them hold a star, at a
// random place inside, kept StarMargin of a cell off its walls. As in the real sky, most stars are faint and few are
// bright (each step of brightness has about three times more stars): a star's brightness is a random number to the
// power StarFaintness, so only about one in ten stands out. Faint ones show white (the eye sees no color in them),
// bright ones blue-white, white, yellowish or orange; bright ones spread a little wider. They twinkle a little, more
// low down, and come out only once night is well on its way (StarsFrom of it). By eye.
const float StarCells           = 90.0;
const float StarChance          = 0.07;
const float StarMargin          = 0.3;
const float StarFaintness       = 10.0;
const float StarSpread          = 0.45;   // a faint star's spread (a Gaussian's sigma), in steps between pixels
const float StarBrightSpread    = 0.5;    // how much wider the brightest ones spread
const float StarTwinkle         = 0.15;   // how much a star's light flickers high up, as a share of it
const float StarLowTwinkle      = 0.4;    // and at the horizon, through more air
const float StarTwinklesPerHour = 2700.0; // 0.75 a second; each star at one, two or three times this
const float StarHaze            = 0.05;   // stars fade out below this height (as the ray's y), about 3 degrees
const float StarsFrom           = 0.35;

// Star colors, by eye after their temperatures: hot blue-white, white, sun-like yellowish, cool orange; the shares up
// to each, from a random number.
const vec3  HotStarColor    = vec3(0.78, 0.86, 1.00);
const vec3  WhiteStarColor  = vec3(1.00, 1.00, 1.00);
const vec3  WarmStarColor   = vec3(1.00, 0.93, 0.80);
const vec3  CoolStarColor   = vec3(1.00, 0.78, 0.55);
const float HotStarShare    = 0.25;
const float WhiteStarShare  = 0.60;
const float WarmStarShare   = 0.85;

// ---- The sun ----

// The disk: about four times the real sun's 0.27 degrees, so it shows well; the glow does the rest. It is measured on
// the picture (the camera's image plane), so it stays round wherever it stands on screen.
const float SunRadius = radians(1.0);

// How much brighter the disk is than the light it gives: enough that a day sun's disk turns white and a setting sun's
// dim, red light still shows a bright yellow-orange ball, clearly brighter than the orange sky around it. By eye.
const float DiskBrightness = 8.0;

// Limb darkening: the sun's rim shows its cooler, outer layers and is darker than its middle, by 1 - u (1 - mu), with u
// about 0.6 for visible light (Allen's Astrophysical Quantities, linear limb darkening law).
const float LimbDarkening = 0.6;

// The glow, as Henyey-Greenstein lobes (Lobe below): a tight glare right around the disk, the eye's or a camera's own
// scattering of a very bright light, and a wide aureole, the light haze scatters slightly off its path. Sharpness near
// 1 keeps a lobe tight; strength is its brightness straight at the sun, as a share of the sun's light. By eye.
const float GlareSharpness   = 0.97; // falls to half about 1.3 degrees from the sun's middle
const float GlareStrength    = 3.0;
const float AureoleSharpness = 0.85; // falls to half about 8 degrees out
const float AureoleStrength  = 0.45;

// The rays: faint streaks out of the sun, as a bright light makes in the eye or a camera, two sets of spikes turning
// slowly the opposite way, fading within about RayReach. They show when the camera looks at the sun (within
// RayLookWide, fully within RayLookNarrow) and fade as it looks away. By eye: there but not loud.
const float RaysA         = 7.0;  // spikes in the first set
const float RaysB         = 11.0; // in the second; not a multiple of the first, so they never line up for long
const float RaySharpness  = 10.0; // how thin a spike is
const float RayStrength   = 0.28; // their light next to the disk, as a share of the sun's light
const float RayReach      = radians(12.0);
const float RayTurnsPerHour = 6.0; // one turn every ten minutes: a movement seen only when looked for
const float RayLookWide   = 0.82;  // cos(35 degrees)
const float RayLookNarrow = 0.985; // cos(10 degrees)

// The breath: the glow swells and fades by GlowBreath of itself, BreathsPerHour times an hour (every 4 seconds).
const float GlowBreath     = 0.04;
const float BreathsPerHour = 900.0;

// ---- The moon ----

// The full moon: as large as the sun (the real ones are too, about 0.26 degrees). Its highlands show nearly white at
// night; by day the same light over the blue sky looks pale. By eye.
const float MoonRadius         = radians(1.0);
const float MoonDiskBrightness = 2.0;

// The moon's seas (maria): dark lava plains, about 0.07 of the light thrown back against about 0.12 for the highlands
// (Allen's Astrophysical Quantities, "Moon"), so MariaDarkness darker. Placed after the full moon as seen from the
// northern half of the earth (x across the disk, -1 to 1, east of the moon on the right; y up): x, y, radius, and how
// dark each is, as a share of MariaDarkness; several overlap into the large joined plains the moon shows. The disk is
// bent by noise before they are laid on it (MoonWarp) and their edges are frayed (MoonRagged), so no sea is a round
// patch; a fine speckle of craters lies over everything (MoonSpeckle). Tycho, a young crater, is brighter.
const float MariaDarkness = 0.45;
const float MariaSize     = 1.3;  // the listed radii are their dark cores; the plains reach this much further
const float MoonWarp      = 0.30;
const float MoonRagged    = 0.45;
const float MoonSpeckle   = 0.14;
const int   MariaCount    = 16;
const vec4  Maria[MariaCount] = vec4[MariaCount](vec4(-0.28, 0.42, 0.28, 1.0),  // Imbrium
                                                 vec4(-0.05, 0.30, 0.14, 0.8),  // Imbrium's south-east shore
                                                 vec4(0.22, 0.43, 0.18, 1.0),   // Serenitatis
                                                 vec4(0.38, 0.12, 0.22, 1.0),   // Tranquillitatis
                                                 vec4(0.24, 0.20, 0.12, 0.9),   // between them
                                                 vec4(0.75, 0.32, 0.13, 1.0),   // Crisium
                                                 vec4(0.62, -0.10, 0.16, 0.9),  // Fecunditatis
                                                 vec4(0.40, -0.28, 0.10, 0.9),  // Nectaris
                                                 vec4(0.00, 0.14, 0.11, 0.8),   // Vaporum and Sinus Medii
                                                 vec4(-0.62, 0.28, 0.27, 0.9),  // Oceanus Procellarum, north
                                                 vec4(-0.58, -0.10, 0.27, 0.9), // Oceanus Procellarum, south
                                                 vec4(-0.30, 0.05, 0.16, 0.8),  // Insularum
                                                 vec4(-0.22, -0.38, 0.17, 0.8), // Nubium
                                                 vec4(-0.50, -0.42, 0.10, 0.9), // Humorum
                                                 vec4(-0.25, 0.72, 0.11, 0.7),  // Frigoris, west
                                                 vec4(0.08, 0.74, 0.10, 0.7));  // Frigoris, east
const vec3  Tycho         = vec3(-0.13, -0.70, 0.06); // x, y, radius
const float TychoBright   = 0.25;

// The halo: the air scatters the moon's light a little off its path, as it does the sun's (Lobe below), a tight bright
// corona and a wide, faint aureole, cool white as the dark-adapted eye sees it. Only at night: by day the bright sky
// hides it. By eye.
const vec3  MoonGlowColor        = vec3(0.80, 0.86, 1.00);
const float MoonGlareSharpness   = 0.96;
const float MoonGlareStrength    = 0.25;
const float MoonAureoleSharpness = 0.80;
const float MoonAureoleStrength  = 0.05;

// The moonlit sky: with the moon up the night sky is no longer black but a deep blue, the day sky's colors at
// MoonSkyLevel of their brightness, fully once the moon stands MoonSkyHeight high (as towardsMoon.y; sin 17 degrees).
// Its light hides the faintest stars all over (MoonStarWash, of a star's brightness) and even bright ones close to it
// (MoonNearWash, falling off like its aureole). By eye.
const float MoonSkyLevel     = 0.08;
const float MoonSkyHeight    = 0.3;
const float MoonStarWash     = 0.05;
const float MoonNearWash     = 0.6;
const float MoonWashSharpness = 0.85;

// ---- The screen ----

// A screen holds 256 steps per color, which show as stripes across a smooth, dark gradient such as a sunset's. A little
// noise, under one step, breaks them up: interleaved gradient noise (Jimenez 2014, "Next Generation Post Processing in
// Call of Duty: Advanced Warfare"), with its published weights.
const float ColorStep         = 1.0 / 255.0;
const vec2  NoisePixelWeights = vec2(0.06711056, 0.00583715);
const float NoiseScale        = 52.9829189;

// Below this a body's direction faces the camera too little to place it on the picture: it is off to the side.
const float OnPicture = 0.2;

// ---- The sky's colors ----

// The day sky's color along ray: a blue gradient above the horizon, eased (1 - (1 - y)^3) so it starts gently at the
// horizon and deepens upwards; below it, a soft fade into the haze, so the horizon shows as a band, not a hard line.
vec3 GetDaySky(vec3 ray)
{
    return ray.y >= 0.0 ? mix(HorizonColor, ZenithColor, 1.0 - pow(1.0 - ray.y, SkyCurve))
                        : mix(HorizonColor, BelowColor, smoothstep(0.0, BelowFade, -ray.y));
}

// The sky's color looking along ray, with no sun, moon or stars in it: the day's gradient, a sunset when the sun
// (towardsSun) stands low, fading into night once it has set.
vec3 GetSkyColor(vec3 ray, vec3 towardsSun)
{
    vec3 day = GetDaySky(ray);

    // How much sunset there is: 0 with the sun 20 degrees up or more (the day sky exactly), 1 from the horizon down.
    // How much this direction faces the sun's side of the sky, seen from above (1 towards it, 0 away), gathered by
    // SunsetFocus; and the band, strongest at the horizon and fading upwards and downwards.
    float sunset  = 1.0 - smoothstep(0.0, SunsetHeight, towardsSun.y);
    vec2  rayFlat = ray.xz / max(length(ray.xz), NoFlatPart);
    vec2  sunFlat = towardsSun.xz / max(length(towardsSun.xz), NoFlatPart);
    float sunSide = pow(0.5 + 0.5 * dot(rayFlat, sunFlat), SunsetFocus);
    float band    = exp(-abs(ray.y) * SunsetBand);
    vec3  horizon = mix(SunsetFarColor, SunsetNearColor, sunSide);
    vec3  evening = mix(day * mix(1.0, TwilightDimming, sunset), horizon, sunset * band);

    // How much night there is: 0 with the sun on the horizon, 1 from NightDepth below it.
    float night    = smoothstep(0.0, NightDepth, -towardsSun.y);
    vec3  darkness = mix(NightHorizonColor, NightZenithColor, sqrt(max(ray.y, 0.0)));
    return mix(evening, darkness, night);
}

// ---- Helpers ----

// How much of a lobe's light reaches a direction angle (radians) away from a light, 1 straight at it: the
// Henyey-Greenstein phase function (Henyey and Greenstein 1941, "Diffuse radiation in the galaxy"), divided by its
// peak, written with sin(angle / 2) so it stays exact close to the light.
float Lobe(float angle, float sharpness)
{
    float halfSine = sin(0.5 * angle);
    float spread   = (1.0 - sharpness) * (1.0 - sharpness);
    return pow(spread / (spread + 4.0 * sharpness * halfSine * halfSine), 1.5);
}

// A value between -0.5 and 0.5 that changes from pixel to pixel without a visible pattern.
float GetDither(vec2 pixel)
{
    return fract(NoiseScale * fract(dot(pixel, NoisePixelWeights))) - 0.5;
}

// Where a direction lands on the camera's picture (its image plane, one unit in front of the camera), in that plane's
// units; right, up and forward are the camera's.
vec2 OnImagePlane(vec3 direction, vec3 right, vec3 up, vec3 forward)
{
    return vec2(dot(direction, right), dot(direction, up)) / max(dot(direction, forward), NoFlatPart);
}

// How much of a sun or moon shows (render/sky_clock.hpp, GetDiskShowing, the same curve): all of it until its middle
// is SetStart below the horizon, by which the ground's far edge hides it, then fading out until SetEnd below. It stays
// round: nothing in the sky cuts it.
const float SetStart = radians(2.0);
const float SetEnd   = radians(6.0);
float GetShowing(vec3 towardsBody)
{
    float height = asin(clamp(towardsBody.y, -1.0, 1.0));
    return smoothstep(-SetEnd, -SetStart, height);
}

// The brightest of a color's three channels.
float GetBrightest(vec3 color)
{
    return max(color.r, max(color.g, color.b));
}

// Smooth noise from 0 to 1 over a plane, changing about once per unit (value noise: random numbers at whole places,
// blended smoothly between them), the same every time: the moon's face is made of it.
float GetValueNoise(vec2 place, uint salt)
{
    vec2  cell   = floor(place);
    vec2  inside = fract(place);
    vec2  ease   = inside * inside * (3.0 - 2.0 * inside);
    float lowerLeft  = GetCellRandom(vec3(cell, 0.0), salt).x;
    float lowerRight = GetCellRandom(vec3(cell + vec2(1.0, 0.0), 0.0), salt).x;
    float upperLeft  = GetCellRandom(vec3(cell + vec2(0.0, 1.0), 0.0), salt).x;
    float upperRight = GetCellRandom(vec3(cell + vec2(1.0, 1.0), 0.0), salt).x;
    return mix(mix(lowerLeft, lowerRight, ease.x), mix(upperLeft, upperRight, ease.x), ease.y);
}

// Value noise in three layers, each twice as fine and half as strong: coarse shapes with finer detail on them.
float GetMoonNoise(vec2 place, uint salt)
{
    float sum    = 0.0;
    float weight = 0.5;
    float total  = 0.0;
    for (int layer = 0; layer < 3; ++layer)
    {
        sum += weight * GetValueNoise(place, salt);
        total += weight;
        place  = place * 2.03 + 17.0; // not exactly twice, so the layers' grids never line up
        weight *= 0.5;
    }
    return sum / total;
}

// ---- Stars, moon and sun ----

// A star's color from a random number from 0 to 1 (StarColor shares).
vec3 GetStarColor(float pick)
{
    if (pick < HotStarShare)
    {
        return HotStarColor;
    }
    if (pick < WhiteStarShare)
    {
        return WhiteStarColor;
    }
    return pick < WarmStarShare ? WarmStarColor : CoolStarColor;
}

// The light of the star (if any) this ray looks at: a small soft point (a Gaussian) about pixel (the ray's change from
// one pixel to the next) across, twinkling a little with seconds. wash is how much of a star's brightness the
// moonlit sky hides: fainter ones do not show.
vec3 GetStarLight(vec3 ray, float pixel, float seconds, float wash)
{
    vec3 cell   = floor(ray * StarCells);
    vec3 chance = GetCellRandom(cell, 0u);
    if (chance.x > StarChance)
    {
        return vec3(0.0);
    }

    // The star lies at a random place inside its cell, kept off the cell's walls; one that lands in another cell once
    // brought onto the sky's sphere is left out, so no star is cut in half by a cell's wall.
    vec3 place = normalize(cell + StarMargin + (1.0 - 2.0 * StarMargin) * GetCellRandom(cell, 1u));
    if (any(notEqual(floor(place * StarCells), cell)))
    {
        return vec3(0.0);
    }
    float brightness = pow(chance.z, StarFaintness) - wash;
    if (brightness <= 0.0)
    {
        return vec3(0.0);
    }

    // Its look: color, how fast it twinkles (a whole number of times an hour, so the clock wraps cleanly) and when.
    vec3  look     = GetCellRandom(cell, 2u);
    float spread   = pixel * (StarSpread + StarBrightSpread * brightness);
    float distance = length(ray - place) / spread;
    float point    = exp(-0.5 * distance * distance);
    float speed    = StarTwinklesPerHour * (1.0 + floor(look.y * 3.0));
    float amount   = mix(StarLowTwinkle, StarTwinkle, smoothstep(0.0, 0.5, ray.y));
    float twinkle  = 1.0 - amount * (0.5 + 0.5 * sin(2.0 * Pi * (look.z + speed * seconds / HourSeconds)));
    vec3  color    = mix(vec3(1.0), GetStarColor(look.x), brightness);
    return color * brightness * point * twinkle;
}

// How much light the moon's rock throws back at a place on its disk (across: -1 to 1 across, y up), 1 for bright
// highland: its seas darker, with ragged edges, a fine speckle of craters over all, Tycho bright.
float GetMoonAlbedo(vec2 across)
{
    // Bend the disk with smooth noise, then lay the seas on it with frayed edges.
    vec2  bend   = vec2(GetMoonNoise(across * 2.0, 3u), GetMoonNoise(across * 2.0 + 31.0, 4u)) - 0.5;
    vec2  place  = across + bend * MoonWarp;
    float ragged = (GetMoonNoise(across * 5.0, 5u) - 0.5) * MoonRagged;
    float sea    = 0.0;
    for (int index = 0; index < MariaCount; ++index)
    {
        vec4  mare = Maria[index];
        float into = 1.0 - smoothstep(0.5, 1.2, length(place - mare.xy) / (mare.z * MariaSize) + ragged);
        sea        = max(sea, into * mare.w);
    }
    float speckle = (GetMoonNoise(across * 9.0, 6u) - 0.5) * MoonSpeckle;
    float tycho   = TychoBright * (1.0 - smoothstep(0.0, Tycho.z, length(across - Tycho.xy)));
    return (1.0 - MariaDarkness * sea) * (1.0 + speckle) + tycho;
}

// ---- The whole sky, for the sky itself and for what mirrors it ----

// The camera's sides, from the way it looks: right level with the horizon, up square to both (straight up or down any
// right will do).
void GetCameraSides(vec3 forward, out vec3 right, out vec3 up)
{
    vec3 side = cross(forward, WorldUp);
    right     = length(side) > NoFlatPart ? normalize(side) : vec3(1.0, 0.0, 0.0);
    up        = cross(right, forward);
}

// The sky along ray, seen by a camera looking along forward (right and up its sides), without the sun: its colors,
// deep blue at night while the moon is up, the stars and the full moon (moonDisk: its tint times its brightness) with
// its halo. towardsSun sets the time of day, seconds the stars' twinkle. Call it where every pixel runs: it measures
// how the ray changes from one pixel to the next.
vec3 GetSkyBehindSun(vec3 ray, vec3 right, vec3 up, vec3 forward, vec3 towardsSun, vec3 towardsMoon, vec3 moonDisk,
                     float seconds)
{
    vec2 rayOnPlane = OnImagePlane(ray, right, up, forward);

    // ---- The sky, deep blue at night while the moon is up ----
    float moonBright = GetBrightest(moonDisk) * GetShowing(towardsMoon);
    float moonUp     = moonBright * smoothstep(0.0, MoonSkyHeight, towardsMoon.y);
    float moonAngle  = acos(clamp(dot(ray, towardsMoon), -1.0, 1.0));
    float night      = smoothstep(0.0, NightDepth, -towardsSun.y);
    vec3  sky        = GetSkyColor(ray, towardsSun) + GetDaySky(ray) * MoonSkyLevel * moonUp * night;

    // ---- Its stars at night above the horizon, fading out low down in the haze and in the moon's light ----
    // (A pixel's step is measured here, where every pixel runs: derivatives are only defined outside branches that
    // differ from pixel to pixel.)
    vec3  color = sky;
    float pixel = max(length(fwidth(ray)), 1.0e-6);
    float stars = smoothstep(StarsFrom, 1.0, night);
    if (stars > 0.0 && ray.y > 0.0)
    {
        float wash = moonBright * (MoonStarWash * moonUp + MoonNearWash * Lobe(moonAngle, MoonWashSharpness));
        color += GetStarLight(ray, pixel, seconds, wash) * stars * smoothstep(0.0, StarHaze, ray.y);
    }

    // ---- The moon: its disk hides the stars behind it, not the air in front (so by day it is pale); its halo ----
    if (moonBright > NoLight && dot(towardsMoon, forward) > OnPicture)
    {
        vec2  offset   = rayOnPlane - OnImagePlane(towardsMoon, right, up, forward);
        float radius   = tan(MoonRadius);
        float across   = length(offset) / radius;
        float edge     = max(fwidth(across), 1.0e-6);
        float covering = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, across);
        if (covering > 0.0)
        {
            vec3 shine = moonDisk * MoonDiskBrightness * GetMoonAlbedo(offset / radius) * GetShowing(towardsMoon);
            vec3 face  = 1.0 - (1.0 - sky) * exp(-shine);
            color      = mix(color, face, covering);
        }
        float halo = MoonGlareStrength * Lobe(moonAngle, MoonGlareSharpness) +
                     MoonAureoleStrength * Lobe(moonAngle, MoonAureoleSharpness);
        color = 1.0 - (1.0 - color) * exp(-MoonGlowColor * halo * moonBright * night);
    }
    return color;
}

// What the sun adds along a ray: the light arriving (disk, glow and rays; not yet built up towards white, so a mirror
// can take its share first) and how much of the ray its disk covers, hiding what lies behind it.
struct SunLook
{
    vec3  arriving;
    float coverage;
};

// What the sun adds along ray, seen by a camera looking along forward (right and up its sides): sunLight is its disk's
// light (color times intensity; none: no sun), seconds the clock its rays and glow move by. Call it where every pixel
// runs.
SunLook GetSunLook(vec3 ray, vec3 right, vec3 up, vec3 forward, vec3 towardsSun, vec3 sunLight, float seconds)
{
    float sunShows  = GetShowing(towardsSun) * step(NoLight, GetBrightest(sunLight));
    float angle     = acos(clamp(dot(ray, towardsSun), -1.0, 1.0));
    float coverage  = 0.0;
    float limb      = 1.0;
    float rays      = 0.0;
    float facingSun = dot(towardsSun, forward);
    if (facingSun > OnPicture)
    {
        // The disk, measured on the picture so it is round anywhere on screen; its edge smoothed over about a pixel.
        vec2  offset = OnImagePlane(ray, right, up, forward) - OnImagePlane(towardsSun, right, up, forward);
        float radius = tan(SunRadius);
        float across = length(offset) / radius;
        float edge   = max(fwidth(across), 1.0e-6);
        coverage     = (1.0 - smoothstep(1.0 - edge, 1.0 + edge, across)) * sunShows;

        // Across the disk: 1 in its middle, darker towards its rim (mu: how squarely that bit of the sun faces us).
        float inside = min(across, 1.0);
        limb         = 1.0 - LimbDarkening * (1.0 - sqrt(1.0 - inside * inside));

        // The rays: two sets of thin spikes around the sun's place on the picture, turning slowly opposite ways,
        // fading with the angle from the sun, and only while the camera looks towards it.
        float around = atan(offset.y, offset.x);
        float turn   = 2.0 * Pi * RayTurnsPerHour * seconds / HourSeconds;
        float spikes = pow(0.5 + 0.5 * cos(RaysA * around + turn), RaySharpness) +
                       pow(0.5 + 0.5 * cos(RaysB * around - turn), RaySharpness);
        float look   = smoothstep(RayLookWide, RayLookNarrow, facingSun);
        rays         = RayStrength * 0.5 * spikes * exp(-angle / RayReach) * look * sunShows;
    }

    // The glow around it, breathing very slightly, fading with the disk as it sets.
    float breath  = 1.0 + GlowBreath * sin(2.0 * Pi * BreathsPerHour * seconds / HourSeconds);
    float glare   = GlareStrength * Lobe(angle, GlareSharpness);
    float aureole = AureoleStrength * Lobe(angle, AureoleSharpness);
    float glow    = (glare + aureole) * breath * sunShows;

    SunLook sun;
    sun.arriving = sunLight * (DiskBrightness * limb * coverage + glow + rays);
    sun.coverage = coverage;
    return sun;
}

// Light arriving over a color, built up towards white smoothly (1 - e^-x per color, over what is already there), as
// in an eye or on film, instead of being cut off flat at full brightness.
vec3 AddLight(vec3 color, vec3 arriving)
{
    return 1.0 - (1.0 - color) * exp(-arriving);
}
