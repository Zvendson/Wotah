// What the water's shaders share (#include "water.glsl", after lighting.glsl): the solid scene behind the water at set
// 1 (render/scene_behind.hpp), how water reflects, bends, takes and scatters light, and how far the solid scene lies at
// a place of the picture.

#ifndef WOTAH_WATER_GLSL
#define WOTAH_WATER_GLSL

// The solid scene behind the water: its depth, 0 at the camera's nearest distance and 1 at its farthest, and a copy of
// its colors, taken before any water was drawn.
layout(set = 1, binding = 0) uniform sampler2D sceneDepth;
layout(set = 1, binding = 1) uniform sampler2D sceneColor;

// How much light water throws back looked at straight down: ((n - 1) / (n + 1))^2 with n = 1.33, about 0.02; at a
// flat angle up to all of it (lighting.glsl, GetFresnel).
const float StraightReflectance = 0.02;

// Light entering water slows to 1 / 1.33 of its speed and bends towards the surface's normal (Snell's law). From
// below, it leaves the water only within an angle whose cosine is CriticalCosine (48.6 degrees) from straight up:
// sqrt(1 - (1 / 1.33)^2). Farther out the surface mirrors all of it (total internal reflection).
const float AirToWater     = 1.0 / 1.33;
const float CriticalCosine = 0.6614;

// How much of each color the water takes per metre: pure water takes red most (about 0.35 per metre at 650 nm) and
// blue least (Pope and Fry 1997, "Absorption spectrum (380-700 nm) of pure water"); a sea's fine particles take some
// of every color on top, by eye. So sand under 3 metres of it shows turquoise. Light from what lies under the water
// crosses it twice: down from the sky, and back up along the view.
const vec3 Absorption = vec3(0.38, 0.09, 0.07);

// The color of the light the water scatters back itself, blue-green, by eye.
const vec3 WaterColor = vec3(0.02, 0.13, 0.16);

// Seen from inside, the water's own light comes from all around, brighter than the water seen from above: this many
// times the water's color, by eye.
const float InsideGlow = 3.0;

// Where nothing solid was drawn (only sky, at the depth image's far value), the water is taken to go on for ever: this
// many metres, so nothing shows through it.
const float NothingBehind = 1.0e6;

// How far in front of the camera the solid scene lies at a place of the picture (0 to 1 across and down), along the
// camera's view: the depth image's value turned back into a distance (glm's perspective with depth from 0 to 1:
// view depth = near far / (far - depth (far - near))); NothingBehind where nothing was drawn.
float GetSolidViewDepth(vec2 picturePlace)
{
    float near  = sceneLights.cameraPosition.w;
    float far   = sceneLights.cameraForward.w;
    float depth = texture(sceneDepth, picturePlace).r;
    return depth >= 1.0 ? NothingBehind : near * far / (far - depth * (far - near));
}

// The light the water itself gives seen from inside, at position (viewDepth in front of the camera, for its shadows),
// depth metres under the surface: the light from above has crossed that much water on its way down.
vec3 GetInsideGlow(vec3 position, float viewDepth, float depth)
{
    return InsideGlow * Light(WaterColor, vec3(0.0, 1.0, 0.0), position, viewDepth) *
           exp(-Absorption * max(depth, 0.0));
}

#endif // WOTAH_WATER_GLSL
