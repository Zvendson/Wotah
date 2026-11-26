#version 450
#extension GL_GOOGLE_include_directive : require

// The sea's surface (source/render/water_renderer.hpp), flat and still for now: a mirror of the sky where it is seen
// at a flat angle (the sun, sunsets, moon and stars in it), clear where it is looked down into, showing what lies under
// it (the sand, the ball) tinted turquoise the deeper it lies (water takes red far more than blue), and lit itself by
// the scene's lights (shadows on it included). How far the view goes through the water comes from the solid scene's
// depth (render/scene_depth.hpp): at the waterline nothing, so the water fades out on the beach; where nothing lies
// behind (open sea) the water is deep and nothing shows from below. Far away it fades into the haze like the land.
// Drawn over the solid scene (BlendMode::Filtered): what lies under the water shows through the filter the second
// output gives.
//
// shortcut: reflections show only the sky, not the land or the shapes above the water.

#include "atmosphere.glsl"
#include "lighting.glsl"
#include "noise.glsl"
#include "sky.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

// The solid scene's depth (render/scene_depth.hpp), as the depth image holds it: 0 at the camera's nearest distance, 1
// at its farthest.
layout(set = 1, binding = 0) uniform sampler2D sceneDepth;

// What the water adds (its mirrored sky, its own glow), and the filter what lies behind it is seen through, per color.
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outThrough;

// How much light water throws back looked at straight down: ((n - 1) / (n + 1))^2 with n = 1.33, about 0.02; at a
// flat angle up to all of it, by Schlick's approximation of the Fresnel equations (Schlick 1994, "An Inexpensive BRDF
// Model for Physically-based Rendering").
const float StraightReflectance = 0.02;
const float SchlickPower        = 5.0;

// How much of each color the water takes per metre: pure water takes red most (about 0.35 per metre at 650 nm) and
// blue least (Pope and Fry 1997, "Absorption spectrum (380-700 nm) of pure water"); a lake's fine particles take some
// of every color on top, by eye. So sand under 3 metres of it shows turquoise. Light from what lies under the water
// crosses it twice: down from the sky, and back up along the view.
const vec3 Absorption = vec3(0.38, 0.09, 0.07);

// The color of the light the water scatters back itself, blue-green, by eye.
const vec3 WaterColor = vec3(0.02, 0.13, 0.16);

// How much light gets through the surface seen from below (the camera under water): a dim window to the sky.
const float FromBelowThrough = 0.5;

// How far the solid scene lies behind this point of the surface, along the view ray (length 1), from the depth image:
// its depth turned back into a distance along the camera's view (glm's perspective with depth from 0 to 1:
// view depth = near far / (far - depth (far - near))), less the surface's own, then along the ray.
float GetDistanceBehind(vec3 ray)
{
    float near       = sceneLights.cameraPosition.w;
    float far        = sceneLights.cameraForward.w;
    float depth      = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    float solidDepth = near * far / (far - depth * (far - near));
    float along      = max(dot(ray, sceneLights.cameraForward.xyz), 1.0e-4);
    return max(solidDepth - inViewDepth, 0.0) / along;
}

void main()
{
    vec3  surface = normalize(inWorldNormal);
    vec3  ray     = normalize(inWorldPosition - sceneLights.cameraPosition.xyz);
    float facing  = -dot(ray, surface); // how squarely the camera looks at the surface: 1 straight down
    vec3  body    = Light(WaterColor, surface, inWorldPosition, inViewDepth);

    // The camera's mirror image below the surface: what it sees along the mirrored ray is the sky the water mirrors,
    // and the mirrored camera's sides keep sun and moon round in it. Evaluated for every pixel (it measures how the ray
    // changes from one to the next), also where it is not used.
    vec3 forward = sceneLights.cameraForward.xyz;
    vec3 right;
    vec3 up;
    GetCameraSides(forward, right, up);
    vec3    mirroredRay = reflect(ray, surface);
    vec3    towardsSun  = sceneLights.skySun.xyz;
    float   seconds     = sceneLights.skySun.w;
    vec3    sky         = GetSkyBehindSun(mirroredRay, reflect(right, surface), reflect(up, surface),
                                          reflect(forward, surface), towardsSun, sceneLights.skyMoon.xyz,
                                          sceneLights.skyMoonLight.rgb, seconds);
    SunLook sun         = GetSunLook(mirroredRay, reflect(right, surface), reflect(up, surface),
                                     reflect(forward, surface), towardsSun, sceneLights.skySunLight.rgb, seconds);

    vec3 color;
    vec3 through; // how much of what lies behind the surface shows, per color
    if (facing > 0.0)
    {
        // From above: the mirrored sky by the Fresnel share, the rest goes into the water. On the way to what lies
        // under it and back the water takes its share of each color and adds its own; what is left of it shows. The
        // view goes through the water as far as the scene lies behind; the light from the sky came down as deep.
        float reflected = StraightReflectance + (1.0 - StraightReflectance) * pow(1.0 - facing, SchlickPower);
        float behind    = GetDistanceBehind(ray);
        float below     = behind * facing;
        vec3  kept      = exp(-Absorption * (behind + below));
        color   = reflected * sky * (1.0 - sun.coverage) + (1.0 - reflected) * (1.0 - kept) * body;
        through = (1.0 - reflected) * kept;

        // The sun mirrored: its full light, by the Fresnel share, builds up over the water towards white, so its
        // reflection stays bright even where the water mirrors little.
        color = AddLight(color, reflected * sun.arriving);
    }
    else
    {
        color   = body * (1.0 - FromBelowThrough);
        through = vec3(FromBelowThrough);
    }

    // Far away the water fades into the haze, which hides what lies behind it too.
    float haze = 1.0 - exp(-inViewDepth * HazeDensity);
    color      = mix(color, sceneLights.hazeColor.rgb, haze);
    through *= 1.0 - haze;

    outColor   = vec4(color + GetDither(gl_FragCoord.xy) * ColorStep, 1.0);
    outThrough = vec4(through, 1.0);
}
