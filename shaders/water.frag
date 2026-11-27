#version 450
#extension GL_GOOGLE_include_directive : require

// The sea's surface (source/render/water_renderer.hpp): flat, with fine ripples that drift with the wind. Looked at
// straight down it is clear and shows the ground under it, bent where the view enters the water (Snell's law) and
// tinted turquoise the deeper it lies (water takes red far more than blue); at a flat angle it mirrors what stands
// above it, sky, land and shapes (the mirror's picture, render/mirror_view.hpp), its ripples breaking the mirror image
// up. The sun and moon glint in the ripples, and the ripples gather the sun's light into bright moving webs on the
// ground under shallow water (caustics). Where it runs shallow over the sand it foams. How far the view goes
// through the water and what lies there come from the solid scene's depth and a copy of its colors
// (render/scene_behind.hpp): at the waterline nothing, so the water fades out on the beach; where nothing lies behind
// (open sea) the water is deep and nothing shows from below. Far away it fades into the haze like the land. Seen
// from below (the camera under water, underwater.frag tinting the rest), it shows the world above only within Snell's
// window and mirrors the water outside it.

#include "atmosphere.glsl"
#include "lighting.glsl"
#include "noise.glsl"
#include "sky.glsl"
#include "water.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: the camera, to find where a bent view lands on the picture
    vec4 level;          // the clipmap level (water.vert)
    vec4 surface;        // x: the surface's height
} push;

// The mirror's picture (render/mirror_view.hpp): what the flat surface mirrors at each place of the picture, and its
// depth, 1 where only the sky shows.
layout(set = 2, binding = 0) uniform sampler2D mirrorColor;
layout(set = 2, binding = 1) uniform sampler2D mirrorDepth;

layout(location = 0) out vec4 outColor;

// ---- Light at the surface ----

// How far in front of this point of the surface (in metres along the view) the picture may show something where the
// bent view lands before the bend is given up: within it the bend fades out, so no hard line shows where it stops.
const float BendFade = 0.6;

// The ripples bend the view into the water a little to one side or the other; this far (metres) along its way through
// the water the bend shows at most, so deep water does not smear what little shows of its floor.
const float BendReach = 1.0;

// How far behind the surface what lies under the water is looked at for which way it faces: farther, only open water
// lies behind, and the numbers would lose their precision.
const float FarestUnder = 1.0e4;

// How rough the surface is for the glints, as GGX's alpha (Walter et al. 2007, "Microfacet Models for Refraction
// through Rough Surfaces"): a still surface would mirror the sun as a disk; the ripples spread its glints. A sharp
// lobe for the glints, a wide faint one for the glow around them, by eye.
const float GlintRoughness = 0.02;
const float GlowRoughness  = 0.2;
const float GlowShare      = 0.015;

// The screen shows far less than the sun's real brightness, so its glints would turn the whole width of their lobe
// white, a flat smear where the low sun or moon is mirrored. They are shown at GlintExposure of their light, as a
// camera's exposure would take them: only their cores turn white, their edges keep a gradient. By eye.
const float GlintExposure = 0.3;

// How far beyond the surface the mirrored view is taken to meet what it mirrors, in metres, when the ripples tilt it
// to the side: the tilt moves the mirror's picture as much as it would move a thing that far away. By eye.
const float ReflectionReach = 3.0;

// How far the ripples may move the mirror's picture down the picture (towards the camera), as a share of its height:
// there the mirror's picture shows what stands between the camera and the water (the beach the camera stands on),
// which the water cannot mirror at that place.
const float MirrorTowardsCamera = 0.004;

// ---- Seen from below ----

// Snell's window's edge is blurred over this much of the cosine to each side, so it shows no hard line.
const float WindowEdge = 0.02;

// ---- Ripples ----

// The ripples: four layers of gradient noise, each drifting its own way at its own speed, from slow, broad swells of
// a few metres to fast little wavelets (a light breeze over calm water); slope is how steep each gets (its height over
// its length). By eye.
struct RippleLayer
{
    float wavelength; // metres
    float slope;
    vec2  drift;      // metres per second
    float period;     // seconds the layer takes to fade from one stretch of noise to the next
    uint  salt;
};
const int         RippleLayerCount = 4;
const RippleLayer RippleLayers[RippleLayerCount] = RippleLayer[](
    RippleLayer(3.1, 0.040, vec2(0.50, 0.18), 8.0, 0x51ED270Bu),
    RippleLayer(1.3, 0.034, vec2(-0.22, 0.30), 6.0, 0x7C0F4E1Du),
    RippleLayer(0.55, 0.028, vec2(0.20, -0.16), 4.5, 0x2F8B6A93u),
    RippleLayer(0.21, 0.022, vec2(-0.14, -0.06), 3.0, 0x0E3D9B57u));

// How much the slopes of ripples left out (finer than a pixel) roughen the glints instead: a ripple's slope squared is
// its share of the surface's spread of slopes (LEAN mapping's idea, Olano and Baker 2010), by eye.
const float LostRippleRoughness = 1.5;

// ---- Foam ----

// Foam forms where water runs out over gently sloping ground (the swash on the beach), and caustics lie flat on the
// floor, not on the steep sides of things standing in the water: both fade out where what lies under the water rises
// more steeply than SteepStart (metres per metre), gone by SteepEnd. The beach rises about 0.06. The slope is measured
// SlopeReach pixels to each side, so the very line where the water meets a thing standing in it counts as steep too
// (right at it, what lies under the water is the water's own level).
const float SteepStart = 0.25;
const float SteepEnd   = 0.6;
const float SlopeReach = 6.0;

// Foam along the waterline: a thin line where the swash ends (water shallower than FoamEdgeDepth metres), and lace
// out to FoamDepth: thin bubbly threads (where noise of FoamWavelength metres crosses zero) that drift slowly (fading
// from one stretch of noise to the next every FoamPeriod seconds, as the ripples do), at most FoamCover of the water;
// the band grows and shrinks by FoamSwash of its width every SwashPeriod seconds, as if small swash ran up the beach
// and back, a little earlier or later along the beach (SwashSpread metres). By eye, after photographs of calm beaches.
const vec3  FoamColor      = vec3(0.92, 0.94, 0.93);
const float FoamEdgeDepth  = 0.03;
const float FoamDepth      = 0.14;
const float FoamCover      = 0.8;
const float FoamThreads    = 5.0; // how thin the threads are: the power the noise's closeness to zero is raised to
const float FoamWavelength = 0.9;
const vec2  FoamDrift      = vec2(0.03, 0.02);
const float FoamPeriod     = 20.0;
const float FoamSwash      = 0.35;
const float SwashPeriod    = 7.5;
const float SwashSpread    = 40.0;
const uint  FoamSalt       = 0x6A09E667u;

// ---- Caustics ----

// The ripples focus the sun's light on the ground under them into a net of bright lines that moves with them
// (caustics): the edges of two moving cellular patterns of CausticCell metres (Worley 1996, "A Cellular Texture Basis
// Function"), each cell's point circling around its middle by CausticWander of a cell, one to three times every
// CausticPeriod seconds, the patterns bent by smooth noise of CausticBend cells so the lines curve. The lines are
// CausticLine of a cell wide just under the surface and blur, widening by CausticBlur per metre of depth, until they
// are gone by CausticDeepest metres; where the two nets cross, the light gathers most. By eye, after photographs of
// sunny shallows.
const float CausticCell      = 0.7;
const float OtherCausticCell = 0.5;
const float CausticWander    = 0.3;
const float CausticBend      = 0.45;
const float CausticPeriod    = 6.0;
const float CausticLine      = 0.05;
const float CausticBlur      = 0.12;
const float CausticStrength  = 0.45;
const float CausticCrossing  = 1.6;
const float CausticDim       = 0.92; // the ground between the lines, as the light gathered there is missing
const float CausticDeepest   = 3.0;
const uint  CausticSalt      = 0x3C6EF372u;

// The sky's clock (SceneLights::SkySun's w) wraps at an hour (render/sky.hpp, SkyLoopSeconds); every period above
// divides it, so nothing jumps when it wraps.

// ---- Ripples ----

// Where two stretches of drifting noise are, and how much of the second shows: each drifts by drift per second over
// a period and then jumps back, half a period apart, the one fading out as it nears its jump while the other fades in
// (Vlachos 2010, "Water Flow in Portal 2"), so the drift goes on for ever without a jump. The places are in units of
// wavelength; the second is moved by half a unit, so the two differ.
void GetDriftingPlaces(vec2 place, vec2 drift, float period, float wavelength, float seconds, out vec2 first,
                       out vec2 second, out float secondShare)
{
    float phase = fract(seconds / period);
    vec2  shift = drift * period;
    first       = (place - shift * phase) / wavelength;
    second      = (place - shift * fract(phase + 0.5)) / wavelength + 0.5;
    secondShare = abs(2.0 * phase - 1.0);
}

// One layer's slope (along x and z) at place, drifting; share is how much of the layer a pixel still shows.
vec2 GetLayerSlope(RippleLayer layer, vec2 place, float seconds, float share)
{
    vec2  first;
    vec2  second;
    float secondShare;
    GetDriftingPlaces(place, layer.drift, layer.period, layer.wavelength, seconds, first, second, secondShare);
    vec2 noiseSlope = mix(GetGradientNoise(first, layer.salt).yz, GetGradientNoise(second, layer.salt).yz,
                          secondShare);
    return layer.slope * share * noiseSlope;
}

// The ripples' slope at place (along x and z) and the spread of slope left out because a pixel covers it (out).
vec2 GetRippleSlope(vec2 place, float seconds, float footprint, out float lostSpread)
{
    vec2 slope = vec2(0.0);
    lostSpread = 0.0;
    for (int index = 0; index < RippleLayerCount; ++index)
    {
        RippleLayer layer = RippleLayers[index];
        float       share = GetLayerShare(layer.wavelength, 2.0 * footprint);
        slope += GetLayerSlope(layer, place, seconds, share);
        lostSpread += layer.slope * layer.slope * (1.0 - share);
    }
    return slope;
}

// ---- The picture ----

// Where a world point lands on the picture, from 0 to 1 across and down (Vulkan's y points down already).
vec2 GetPicturePlace(vec3 point)
{
    vec4 clip = push.viewProjection * vec4(point, 1.0);
    return clip.xy / max(clip.w, 1.0e-4) * 0.5 + 0.5;
}

// How far the solid scene lies behind this point of the surface at a place of the picture, along the view ray (length
// 1): its view depth less the surface's, then along the ray.
float GetDistanceBehind(vec2 picturePlace, vec3 ray)
{
    float along = max(dot(ray, sceneLights.cameraForward.xyz), 1.0e-4);
    return max(GetSolidViewDepth(picturePlace) - inViewDepth, 0.0) / along;
}

// Where on the picture what lies under the water shows, and where it is (out): what lies straight behind this point
// of the surface (path metres away through the water), moved as far as the ripples bend the view away from the way a
// flat surface bends it (Snell's law), over that way through the water (at most BendReach). A flat surface's own bend
// is left out: it would need the shape of what lies under the water, and only lifts it a little; guessed from a flat
// floor, it moved round things (the ball) a long way off their place. A bent view goes on away from the camera, so
// where it would land on something nearer than this point (a shape in front) the bend fades out.
vec2 FindBentGround(vec3 ray, vec3 surface, float path, vec2 straight, out vec3 ground)
{
    vec3  flatBent    = refract(ray, vec3(0.0, 1.0, 0.0), AirToWater);
    vec3  rippledBent = refract(ray, surface, AirToWater);
    vec3  straightEnd = inWorldPosition + ray * min(path, NothingBehind);
    vec3  bentEnd     = straightEnd + (rippledBent - flatBent) * min(path, BendReach);
    vec2  place       = clamp(GetPicturePlace(bentEnd), vec2(0.0), vec2(1.0));
    float onBent      = smoothstep(-BendFade, 0.0, GetSolidViewDepth(place) - inViewDepth);
    ground            = mix(straightEnd, bentEnd, onBent);
    return mix(straight, place, onBent);
}

// Where on the mirror's picture the rippled surface's mirror image is: where the flat surface mirrors (this pixel's
// place), moved as far as the ripples tilt the mirrored view over ReflectionReach (seen in the mirror: that point's
// height mirrored in the surface), but hardly towards the camera.
vec2 FindMirrored(vec3 ray, vec3 surface, vec2 straight)
{
    vec3 target = inWorldPosition + reflect(ray, surface) * ReflectionReach;
    target.y    = 2.0 * push.surface.x - target.y;
    vec2 place  = GetPicturePlace(target);
    place.y     = min(place.y, straight.y + MirrorTowardsCamera);
    return clamp(place, vec2(0.0), vec2(1.0));
}

// ---- Light ----

// The glints of every light in the ripples: the sun's (and at night the moon's) light mirrored by facets turned just
// so, as a sharp lobe and a faint wide one, by the Fresnel share at that angle, blocked by shadows like any light.
vec3 GetGlints(vec3 surface, vec3 towardsCamera, vec3 position, float roughness)
{
    vec3 glints = vec3(0.0);
    uint used   = min(sceneLights.count.x, uint(WOTAH_MAX_DIRECTIONAL_LIGHTS));
    for (uint index = 0; index < used; ++index)
    {
        DirectionalLight light        = sceneLights.directional[index];
        vec3             towardsLight = light.towardsLight.xyz;
        float            share =
            GetGlossShare(surface, towardsLight, towardsCamera, roughness, StraightReflectance) +
            GlowShare * GetGlossShare(surface, towardsLight, towardsCamera, GlowRoughness, StraightReflectance);
        if (share > 0.0)
        {
            float facing = dot(surface, towardsLight);
            glints += light.radiance.rgb * share * GetLightReaching(index, position, surface, facing, inViewDepth);
        }
    }
    return glints;
}

// ---- Foam ----

// How much foam covers the water at position, where it is depth metres deep: the thin line where the swash ends and
// lace out from it, breathing with the swash; finer than a pixel the lace blends into a faint band.
float GetFoam(vec3 position, float depth, float seconds, float footprint)
{
    float late  = GetGradientNoise(position.xz / SwashSpread, FoamSalt).x;
    float swash = 1.0 + FoamSwash * sin(2.0 * Pi * (seconds / SwashPeriod + late));
    float edge  = 1.0 - smoothstep(0.0, FoamEdgeDepth * swash, depth);
    float band  = 1.0 - smoothstep(0.0, FoamDepth * swash, depth);

    vec2  first;
    vec2  second;
    float secondShare;
    GetDriftingPlaces(position.xz, FoamDrift, FoamPeriod, FoamWavelength, seconds, first, second, secondShare);
    float smallest = 2.0 * footprint / FoamWavelength;
    float noise    = mix(GetLayeredNoise(first, FoamSalt, 4, smallest), GetLayeredNoise(second, FoamSalt, 4, smallest),
                         secondShare);
    float threads  = pow(1.0 - min(abs(noise) * 2.0, 1.0), FoamThreads);
    float lace     = mix(0.25, threads, GetLayerShare(FoamWavelength, 2.0 * footprint));
    return FoamCover * max(edge, band * lace);
}

// ---- Caustics ----

// How much of what lies under the water is gently sloping ground (1) rather than a steep side (0), from its slope.
float GetGentleShare(float slope)
{
    return 1.0 - smoothstep(SteepStart, SteepEnd, slope);
}

// How steeply what lies under the water (under, behind this pixel) rises around it: the steepest slope from it to what
// lies under the pixels SlopeReach away to each side, each found from the depth there along the view ray there (this
// pixel's ray, moved as it changes from pixel to pixel: rayDx, rayDy).
float GetUnderSlope(vec2 onPicture, vec3 ray, vec3 under, vec3 rayDx, vec3 rayDy)
{
    vec2  pixel   = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec3  camera  = sceneLights.cameraPosition.xyz;
    vec3  forward = sceneLights.cameraForward.xyz;
    float steepest = 0.0;
    for (int side = 0; side < 4; ++side)
    {
        vec2  step    = side < 2 ? vec2(side == 0 ? 1.0 : -1.0, 0.0) : vec2(0.0, side == 2 ? 1.0 : -1.0);
        vec3  aside   = normalize(ray + SlopeReach * (step.x * rayDx + step.y * rayDy));
        float depth   = GetSolidViewDepth(onPicture + SlopeReach * step * pixel);
        vec3  there   = camera + aside * min(depth / max(dot(aside, forward), 1.0e-4), FarestUnder);
        vec3  between = there - under;
        steepest      = max(steepest, abs(between.y) / max(length(between.xz), 1.0e-3));
    }
    return steepest;
}

// How close place (in cells) is to an edge of a moving cellular pattern: the distance to the second-nearest cell
// point less the distance to the nearest, 0 on the edges. Each cell's point circles its middle a whole number of times
// every CausticPeriod seconds; the place is bent by smooth noise first, so the edges curve.
float GetCellEdge(vec2 place, float seconds, uint salt)
{
    place += CausticBend * vec2(GetGradientNoise(place * 0.5, salt).x, GetGradientNoise(place * 0.5 + 7.3, salt).x);
    vec2  cell    = floor(place);
    float nearest = 8.0;
    float second  = 8.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2  neighbour = cell + vec2(x, y);
            vec3  random    = GetCellRandom(vec3(neighbour, 0.0), salt);
            float turns     = floor(random.z * 3.0) + 1.0;
            float angle     = 2.0 * Pi * (random.x + turns * seconds / CausticPeriod);
            vec2  point     = neighbour + 0.5 + CausticWander * vec2(cos(angle), sin(angle));
            float distance  = length(place - point);
            second          = distance < nearest ? nearest : min(second, distance);
            nearest         = min(nearest, distance);
        }
    }
    return second - nearest;
}

// How much brighter the ground at ground (world) looks under depth metres of water from the caustics, as a factor on
// its color: two nets of light lines, sharp a little under the surface, blurred and gone by CausticDeepest; the share
// of the ground's light that is direct sunlight (litShare) is what they gather. A line is never thinner than the pixel
// it lies in (footprint metres): drawn thinner it would flicker and show bands far away, so it is widened and dimmed
// to match, keeping its light; where the cells themselves are finer than the pixels, only their average is left.
float GetCaustics(vec3 ground, float depth, float seconds, float footprint, float litShare)
{
    float present = smoothstep(0.02, 0.3, depth) * (1.0 - smoothstep(0.6, CausticDeepest, depth));
    float detail  = GetLayerShare(CausticCell, 4.0 * footprint);
    float sharp   = CausticLine + CausticBlur * depth;
    float width   = max(sharp, 1.5 * footprint / OtherCausticCell);
    float dimmed  = sharp / width;
    float lines   = dimmed * (1.0 - smoothstep(0.0, width, GetCellEdge(ground.xz / CausticCell, seconds, CausticSalt)));
    float others  = dimmed * (1.0 - smoothstep(0.0, width, GetCellEdge(ground.xz / OtherCausticCell + 0.5, seconds,
                                                                      CausticSalt + 1u)));
    float gathered = CausticDim + CausticStrength * (lines + others + CausticCrossing * lines * others);
    return mix(1.0, gathered, present * detail * litShare);
}

// The share of the light on flat ground at position that comes straight from the lights (not the sky's ambient light),
// shadows included: what the water's ripples can gather into caustics.
float GetDirectShare(vec3 position, float viewDepth)
{
    vec3 direct = vec3(0.0);
    uint used   = min(sceneLights.count.x, uint(WOTAH_MAX_DIRECTIONAL_LIGHTS));
    for (uint index = 0; index < used; ++index)
    {
        DirectionalLight light  = sceneLights.directional[index];
        float            facing = light.towardsLight.y;
        if (facing > 0.0)
        {
            direct += light.radiance.rgb * facing *
                      GetLightReaching(index, position, vec3(0.0, 1.0, 0.0), facing, viewDepth);
        }
    }
    float all = dot(direct + sceneLights.ambient.rgb, vec3(1.0));
    return all > 0.0 ? dot(direct, vec3(1.0)) / all : 0.0;
}

void main()
{
    float     seconds   = sceneLights.skySun.w;
    vec3      ray       = normalize(inWorldPosition - sceneLights.cameraPosition.xyz);
    vec2      onPicture = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
    float     footprint = length(fwidth(inWorldPosition.xz));

    // How far the view goes through the water to what lies straight behind, how deep that lies under the surface, and
    // how steeply it rises (measured here, where every pixel runs: derivatives are only defined outside branches).
    float path       = GetDistanceBehind(onPicture, ray);
    float depthBelow = min(path, NothingBehind) * max(-ray.y, 0.0);
    vec3  under      = inWorldPosition + ray * min(path, FarestUnder);
    float underSlope = GetUnderSlope(onPicture, ray, under, dFdx(ray), dFdy(ray));

    // The rippled surface: its normal, and how rough it looks where ripples are too fine for the pixel.
    float lostSpread;
    vec2  slope     = GetRippleSlope(inWorldPosition.xz, seconds, footprint, lostSpread);
    vec3  surface   = normalize(inWorldNormal + vec3(-slope.x, 0.0, -slope.y));
    float roughness = sqrt(GlintRoughness * GlintRoughness + LostRippleRoughness * lostSpread);

    // The camera under the surface: within Snell's window the world above shows through, less what the surface mirrors;
    // outside it the surface mirrors the water below entirely. On the way up to the surface the water takes its share,
    // as underwater.frag does for everything else the camera sees.
    vec3 body = Light(WaterColor, surface, inWorldPosition, inViewDepth);
    if (ray.y > 0.0)
    {
        vec3  camera  = sceneLights.cameraPosition.xyz;
        vec3  above   = texture(sceneColor, onPicture).rgb;
        float window  = smoothstep(CriticalCosine - WindowEdge, CriticalCosine + WindowEdge, ray.y);
        float through = window * (1.0 - GetFresnel(ray.y, StraightReflectance));
        float way     = inViewDepth / max(dot(ray, sceneLights.cameraForward.xyz), 1.0e-4);
        vec3  kept    = exp(-Absorption * way);
        vec3  mirrors = GetInsideGlow(inWorldPosition, inViewDepth, 0.0);
        vec3  seen    = mix(mirrors, above, through);
        vec3  glow    = GetInsideGlow(camera, 0.0, push.surface.x - camera.y);
        outColor      = vec4(kept * seen + (1.0 - kept) * glow + GetDither(gl_FragCoord.xy) * ColorStep, 1.0);
        return;
    }

    // How much the surface mirrors, by the angle the camera looks at it.
    vec3  towardsCamera = -ray;
    float facing        = max(dot(surface, towardsCamera), 0.0);
    float reflected     = GetFresnel(facing, StraightReflectance);

    // What it mirrors, and whether only sky is mirrored there (then the sun's and moon's glints show).
    vec2  inMirror    = FindMirrored(ray, surface, onPicture);
    vec3  mirror      = texture(mirrorColor, inMirror).rgb;
    float skyMirrored = step(1.0, texture(mirrorDepth, inMirror).r);

    // What lies under it, along the view bent into the water; on the way there and back up the water takes its share
    // of each color (the light from the sky came down as deep as the ground lies). The ground there gathers the
    // caustics' light.
    vec3  groundPoint;
    vec2  inScene     = FindBentGround(ray, surface, path, onPicture, groundPoint);
    vec3  kept        = exp(-Absorption * (path + depthBelow));
    float groundDepth = max(push.surface.x - groundPoint.y, 0.0);
    float litShare    = GetDirectShare(groundPoint, GetSolidViewDepth(inScene)) * GetGentleShare(underSlope);
    vec3  ground      = texture(sceneColor, inScene).rgb *
                   GetCaustics(groundPoint, groundDepth, seconds, footprint, litShare);

    // Into the water goes what is not mirrored: the ground as much as the water keeps of it, the water's own color for
    // the rest. Then the glints, built up towards white (they are far brighter than the screen can show).
    vec3 color = reflected * mirror + (1.0 - reflected) * (kept * ground + (1.0 - kept) * body);
    vec3 glints = GlintExposure * skyMirrored * GetGlints(surface, towardsCamera, inWorldPosition, roughness);
    color       = AddLight(color, glints);

    // Foam where the water runs shallow over gently sloping ground, lit like the ground.
    float foam = GetFoam(inWorldPosition, depthBelow, seconds, footprint) * GetGentleShare(underSlope);
    color      = mix(color, Light(FoamColor, vec3(0.0, 1.0, 0.0), inWorldPosition, inViewDepth), foam);

    // Far away the water fades into the haze; the ground seen through it had its own haze already.
    color    = ApplyHaze(color, inViewDepth, sceneLights.hazeColor.rgb);
    outColor = vec4(color + GetDither(gl_FragCoord.xy) * ColorStep, 1.0);
}
