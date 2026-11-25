// The scene's lights, shared by every fragment shader (#include "lighting.glsl"): an ambient color that reaches every
// surface, and up to WOTAH_MAX_DIRECTIONAL_LIGHTS directional lights (the sun, the moon), each with its own color and
// its own shadows. Written every frame by source/render/lighting.cpp; WOTAH_MAX_DIRECTIONAL_LIGHTS and
// WOTAH_SHADOW_CASCADES come from CMakeLists.txt, the same numbers the C++ side uses.

// One directional light; matches GpuDirectionalLight in source/render/lighting.cpp (std140: vec4s and mat4s).
struct DirectionalLight
{
    vec4 towardsLight;                              // xyz: towards the light, length 1
    vec4 radiance;                                  // rgb: color times intensity
    vec4 shadow;                                    // x: 1 when the light casts shadows
    mat4 cascadeViews[WOTAH_SHADOW_CASCADES];       // world space to each cascade's shadow map, nearest first
};

layout(std140, set = 0, binding = 0) uniform SceneLights
{
    vec4             ambient;       // rgb: the light reaching every surface
    uvec4            count;         // x: how many directional lights are used
    vec4             cascadeEnds;   // where each cascade ends, along the camera's view
    vec4             cascadeTexels; // world units per map pixel in each cascade
    vec4             cascadeBlend;  // x: the share of a cascade at its far end where it blends into the next
    DirectionalLight directional[WOTAH_MAX_DIRECTIONAL_LIGHTS];
} sceneLights;

// Every light's shadow maps, WOTAH_SHADOW_CASCADES layers per light at layer light * cascades + cascade
// (source/render/shadow_maps.hpp). The sampler compares: 1 where the surface is not behind what the light sees there,
// 0 where it is, blended between map pixels.
layout(set = 0, binding = 1) uniform sampler2DArrayShadow shadowMaps;

// How far a surface point is lifted towards its outside before it is looked up, in map pixels of its cascade: a little
// always, more the more the light grazes the surface. Without it a lit surface partly shadows itself in a striped
// pattern (shadow acne), as the map holds its depth only once per pixel.
const float ShadowLiftAlways  = 0.5;
const float ShadowLiftGrazing = 1.5;

// How many map pixels the shadow's edge is blended over to each side: 3 x 3 lookups, so edges are smooth, not stepped;
// the nearest cascade's pixels are a few millimetres, so the edge stays sharp.
const int ShadowFilterReach = 1;

// How much of a light reaches a point in one cascade's map (0 in full shadow, 1 fully lit).
float LookUpShadow(uint lightIndex, uint cascade, vec3 position, vec3 surface, float facing)
{
    DirectionalLight light = sceneLights.directional[lightIndex];
    float            pixel = sceneLights.cascadeTexels[cascade];

    vec3 lifted = position + surface * pixel * (ShadowLiftAlways + ShadowLiftGrazing * (1.0 - facing));
    vec4 inMap  = light.cascadeViews[cascade] * vec4(lifted, 1.0);
    vec3 spot   = inMap.xyz / inMap.w; // orthographic, so w is 1; divided anyway in case that changes
    vec2 where  = spot.xy * 0.5 + 0.5;

    // Off the map nothing was drawn, so the light reaches there.
    if (any(lessThan(where, vec2(0.0))) || any(greaterThan(where, vec2(1.0))) || spot.z > 1.0)
    {
        return 1.0;
    }

    float layer   = float(lightIndex * uint(WOTAH_SHADOW_CASCADES) + cascade);
    vec2  step    = 1.0 / vec2(textureSize(shadowMaps, 0).xy);
    float reached = 0.0;
    for (int y = -ShadowFilterReach; y <= ShadowFilterReach; ++y)
    {
        for (int x = -ShadowFilterReach; x <= ShadowFilterReach; ++x)
        {
            reached += texture(shadowMaps, vec4(where + vec2(x, y) * step, layer, spot.z));
        }
    }
    float lookups = float((2 * ShadowFilterReach + 1) * (2 * ShadowFilterReach + 1));
    return reached / lookups;
}

// How much of a light reaches a point viewDepth in front of the camera, facing along surface (length 1), where facing
// is how squarely the light falls on it (above 0): from the nearest cascade that covers the point, blended into the next
// one near its far end, so the change of sharpness shows no line. Past the last cascade there are no shadows; they fade
// out over its far end.
float GetLightReaching(uint lightIndex, vec3 position, vec3 surface, float facing, float viewDepth)
{
    if (sceneLights.directional[lightIndex].shadow.x < 0.5)
    {
        return 1.0; // this light casts no shadows
    }

    float previousEnd = 0.0;
    for (uint cascade = 0; cascade < uint(WOTAH_SHADOW_CASCADES); ++cascade)
    {
        float end = sceneLights.cascadeEnds[cascade];
        if (viewDepth > end)
        {
            previousEnd = end;
            continue;
        }

        float reached    = LookUpShadow(lightIndex, cascade, position, surface, facing);
        float blendStart = end - sceneLights.cascadeBlend.x * (end - previousEnd);
        if (viewDepth <= blendStart)
        {
            return reached;
        }
        float toNext = (viewDepth - blendStart) / (end - blendStart);
        float next   = cascade + 1 < uint(WOTAH_SHADOW_CASCADES) ?
                           LookUpShadow(lightIndex, cascade + 1, position, surface, facing) :
                           1.0; // the last one fades out
        return mix(reached, next, toNext);
    }
    return 1.0; // beyond the shadows' reach
}

// A color lit by the scene, for a surface at position (world space), viewDepth in front of the camera, facing along
// normal (any length): the ambient light, plus every directional light by how squarely it falls on the surface (Lambert)
// and how much of it is not blocked on the way (its shadow maps).
vec3 Light(vec3 color, vec3 normal, vec3 position, float viewDepth)
{
    vec3 surface  = normalize(normal);
    vec3 received = sceneLights.ambient.rgb;
    uint used     = min(sceneLights.count.x, uint(WOTAH_MAX_DIRECTIONAL_LIGHTS));
    for (uint index = 0; index < used; ++index)
    {
        DirectionalLight light  = sceneLights.directional[index];
        float            facing = dot(surface, light.towardsLight.xyz);
        if (facing <= 0.0)
        {
            continue; // turned away: this light does not reach it, shadow or not
        }
        received += light.radiance.rgb * facing * GetLightReaching(index, position, surface, facing, viewDepth);
    }
    return color * received;
}
