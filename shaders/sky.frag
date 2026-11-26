#version 450
#extension GL_GOOGLE_include_directive : require

// A clear sky without clouds, through day and night. By day deep blue overhead fading to a pale blue at the
// horizon; with a low sun a sunset, orange on the sun's side and dusky mauve away from it; at night near black with
// stars, deep blue while the moon is up. The sun: a round disk far brighter than the sky, white-hot by day and orange
// low over the horizon, darker towards its rim, with a glare and faint rays that turn and shimmer very slightly when
// looked at, and the sky brightening around it. The full moon: a bright disk with darker, ragged seas, warm low down,
// a soft halo around it, the faintest stars lost in its light; pale over the day sky. Sun and moon stay round and
// whole as they set, sink behind the ground and only then fade. Dithered, so gradients show no stripes.

#include "atmosphere.glsl"
#include "sky.glsl"

layout(location = 0) in vec2 inScreenPosition;

layout(push_constant) uniform PushConstants
{
    mat4 inverseViewProjection; // clip space back to world space: undoes the camera
    vec4 towardsSun;            // xyz: towards the sun, length 1; w: seconds on the sky's clock (repeats every hour)
    vec4 sunLight;              // rgb: the sun's disk, color times intensity (render/sky_lights.hpp, SunDisk)
    vec4 towardsMoon;           // xyz: towards the moon, length 1
    vec4 moonLight;             // rgb: the moon's disk, its tint times its brightness (by day too); none: no moon
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    // The direction this pixel looks in, and the camera's own directions (from the middle of the picture), for
    // placing sun and moon on the picture so they stay round.
    vec3 nearPoint;
    vec3 farPoint;
    GetViewRay(push.inverseViewProjection, inScreenPosition, nearPoint, farPoint);
    vec3 ray = normalize(farPoint - nearPoint);
    GetViewRay(push.inverseViewProjection, vec2(0.0), nearPoint, farPoint);
    vec3 forward = normalize(farPoint - nearPoint);
    vec3 right;
    vec3 up;
    GetCameraSides(forward, right, up);

    float   seconds = push.towardsSun.w;
    vec3    sky     = GetSkyBehindSun(ray, right, up, forward, push.towardsSun.xyz, push.towardsMoon.xyz,
                                      push.moonLight.rgb, seconds);
    SunLook sun     = GetSunLook(ray, right, up, forward, push.towardsSun.xyz, push.sunLight.rgb, seconds);

    // The sun's disk hides the sky behind it; its light then builds up towards white.
    vec3 color = AddLight(sky * (1.0 - sun.coverage), sun.arriving);
    outColor   = vec4(color + GetDither(gl_FragCoord.xy) * ColorStep, 1.0);
}
