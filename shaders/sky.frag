#version 450
#extension GL_GOOGLE_include_directive : require

// A clear daytime sky without clouds: deep blue overhead fading to a pale blue at the horizon, a hazy blue-gray below
// it, and the sun as a bright disk with a soft glow around it, exactly where the sun light comes from.

#include "atmosphere.glsl"

layout(location = 0) in vec2 inScreenPosition;

layout(push_constant) uniform PushConstants
{
    mat4 inverseViewProjection; // clip space back to world space: undoes the camera
    vec4 towardsSun;            // xyz: towards the sun, length 1
    vec4 sunColor;              // rgb: the sun's color
} push;

layout(location = 0) out vec4 outColor;

const vec3  ZenithColor     = vec3(0.16, 0.38, 0.78); // straight up: deep blue
const vec3  BelowColor      = vec3(0.30, 0.36, 0.42); // below the horizon: blue-gray haze, seen only from under the ground
const float SkyCurve        = 3.0;  // the blue deepens fast just above the horizon, then evens out towards the top
const float BelowFade       = 0.25; // the haze reaches its full color this far below the horizon (as the ray's y)
const float SunRadius       = radians(1.0);  // the disk's size; the real sun is 0.27 degrees, too small to see well
const float SunEdge         = radians(0.25); // the disk's soft rim, so it does not look cut out
const float GlowNear        = 0.35; // a bright glow close around the disk
const float GlowNearPower   = 400.0;
const float GlowFar         = 0.15; // a wide, faint glow that warms the sky around the sun
const float GlowFarPower    = 12.0;

void main()
{
    // The direction this pixel looks in.
    vec3 nearPoint;
    vec3 farPoint;
    GetViewRay(push.inverseViewProjection, inScreenPosition, nearPoint, farPoint);
    vec3 ray = normalize(farPoint - nearPoint);

    // Blue gradient above the horizon, eased (1 - (1 - y)^3) so it starts gently at the horizon and deepens upwards;
    // below it, a soft fade into the haze, so the horizon shows as a band, not as a hard line.
    vec3 sky = ray.y >= 0.0 ? mix(HorizonColor, ZenithColor, 1.0 - pow(1.0 - ray.y, SkyCurve))
                            : mix(HorizonColor, BelowColor, smoothstep(0.0, BelowFade, -ray.y));

    // The sun: a glow that grows towards it, and the disk itself.
    float towards = max(dot(ray, push.towardsSun.xyz), 0.0);
    float glow    = GlowNear * pow(towards, GlowNearPower) + GlowFar * pow(towards, GlowFarPower);
    float angle   = acos(clamp(dot(ray, push.towardsSun.xyz), -1.0, 1.0));
    float disk    = 1.0 - smoothstep(SunRadius, SunRadius + SunEdge, angle);

    vec3 color = sky + push.sunColor.rgb * glow;
    color      = mix(color, push.sunColor.rgb, disk);
    outColor   = vec4(min(color, vec3(1.0)), 1.0);
}
