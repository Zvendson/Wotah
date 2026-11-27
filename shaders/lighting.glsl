// The scene's lights, shared by every fragment shader (#include "lighting.glsl"): an ambient color that reaches every
// surface, light bounced up from the ground, the haze's color, the camera and the sky's sun and moon (for mirrors), and
// up to WOTAH_MAX_DIRECTIONAL_LIGHTS directional lights (the sun, the moon), each with its own color and shadows.
// Written every frame by source/render/lighting.cpp; WOTAH_MAX_DIRECTIONAL_LIGHTS and WOTAH_SHADOW_CASCADES come from
// CMakeLists.txt, the same numbers the C++ side uses.

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
    vec4             ambient;        // rgb: the light reaching every surface
    vec4             groundBounce;   // rgb: the light the ground throws back up, reaching surfaces that face down
    vec4             hazeColor;      // rgb: the color far things fade into (atmosphere.glsl, ApplyHaze)
    vec4             cameraPosition; // xyz: where the camera stands; w: its nearest distance
    vec4             cameraForward;  // xyz: the way the camera looks, length 1; w: its farthest distance
    vec4             skySun;         // xyz: towards the sky's sun disk, length 1; w: the sky's clock in seconds
    vec4             skySunLight;    // rgb: the sun's disk, color times intensity (sky.glsl, GetSunLook)
    vec4             skyMoon;        // xyz: towards the sky's moon disk, length 1
    vec4             skyMoonLight;   // rgb: the moon's disk, tint times brightness (sky.glsl, GetSkyBehindSun)
    uvec4            count;          // x: how many directional lights are used
    vec4             cascadeEnds;    // where each cascade ends, along the camera's view
    vec4             cascadeTexels;  // world units per map pixel in each cascade
    vec4             cascadeBlend;   // x: the share of a cascade at its far end where it blends into the next
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
// is how squarely the light falls on it (above 0): from the nearest cascade that covers the point, blended into the
// next one near its far end, so the change of sharpness shows no line. Past the last cascade there are no shadows;
// they fade out over its far end.
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

// How much light a surface throws back looked at along cosine (the cosine of the angle to its normal), for a
// reflectance looked at straight on: Schlick's approximation of the Fresnel equations (Schlick 1994, "An Inexpensive
// BRDF Model for Physically-based Rendering").
float GetFresnel(float cosine, float reflectance)
{
    return reflectance + (1.0 - reflectance) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}

// GGX's share of a rough surface's facets turned to halfway between light and view (Walter et al. 2007, "Microfacet
// Models for Refraction through Rough Surfaces"), halfwayFacing the cosine between the normal and that halfway
// direction, alpha the roughness.
float GetFacetShare(float halfwayFacing, float alpha)
{
    const float pi           = 3.14159265;
    float       alphaSquared = alpha * alpha;
    float       spread       = halfwayFacing * halfwayFacing * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / (pi * spread * spread);
}

// The share of a light a rough mirror throws towards the camera: GGX's facets turned just so, the Fresnel share of
// reflectance at that angle, and how much the facets hide each other from the light and the camera (Smith's
// height-correlated masking and shadowing, Heitz 2014, "Understanding the Masking-Shadowing Function in
// Microfacet-Based BRDFs"), times the light's cosine. Without that last part a surface seen at a flat angle would throw
// back far more light than it gets. surface, towardsLight and towardsCamera have length 1.
float GetGlossShare(vec3 surface, vec3 towardsLight, vec3 towardsCamera, float roughness, float reflectance)
{
    float facing = dot(surface, towardsLight);
    float seen   = dot(surface, towardsCamera);
    if (facing <= 0.0 || seen <= 0.0)
    {
        return 0.0;
    }
    vec3  halfway      = normalize(towardsLight + towardsCamera);
    float alphaSquared = roughness * roughness;
    float hidden       = facing * sqrt(seen * seen * (1.0 - alphaSquared) + alphaSquared) +
                   seen * sqrt(facing * facing * (1.0 - alphaSquared) + alphaSquared);
    float visible = 0.5 / max(hidden, 1.0e-6);
    return GetFacetShare(max(dot(surface, halfway), 0.0), roughness) * visible *
           GetFresnel(dot(halfway, towardsCamera), reflectance) * facing;
}

// The light a glossy film (water on wet sand) at position throws towards the camera: every directional light mirrored
// as rough as roughness (GetGlossShare), blocked by shadows like any light. surface is the normal (length 1),
// towardsCamera the direction to the camera (length 1).
vec3 LightGloss(vec3 surface, vec3 towardsCamera, vec3 position, float viewDepth, float roughness, float reflectance)
{
    vec3 gloss = vec3(0.0);
    uint used  = min(sceneLights.count.x, uint(WOTAH_MAX_DIRECTIONAL_LIGHTS));
    for (uint index = 0; index < used; ++index)
    {
        DirectionalLight light  = sceneLights.directional[index];
        float            share  = GetGlossShare(surface, light.towardsLight.xyz, towardsCamera, roughness, reflectance);
        float            facing = dot(surface, light.towardsLight.xyz);
        if (share > 0.0)
        {
            gloss += light.radiance.rgb * share * GetLightReaching(index, position, surface, facing, viewDepth);
        }
    }
    return gloss;
}

// A color lit by the scene, for a surface at position (world space), viewDepth in front of the camera, facing along
// normal (any length): the ambient light, the ground's bounced light by how much the surface faces down, plus every
// directional light by how squarely it falls on the surface (Lambert) and how much of it is not blocked on the way (its
// shadow maps).
vec3 Light(vec3 color, vec3 normal, vec3 position, float viewDepth)
{
    vec3 surface = normalize(normal);

    // How much of the ground a surface sees: all of it facing straight down, half from an upright side, none facing up.
    float seesGround = 0.5 - 0.5 * surface.y;
    vec3  received   = sceneLights.ambient.rgb + sceneLights.groundBounce.rgb * seesGround;
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
