#version 450
#extension GL_GOOGLE_include_directive : require

// The world seen from under the sea's surface (source/render/water_renderer.hpp), drawn over the whole picture
// (fullscreen.vert) before the surface while the camera is below it: everything the camera sees directly lies in the
// water, so each pixel of the solid scene fades into the water's own color over the way through the water to it, red
// first (water.glsl, Absorption). Where nothing solid lies behind, only water shows. The water's own color is lit as
// deep as the camera is: the light from above has crossed that much water. What lies above the surface is drawn by the
// surface itself, over this.

#include "atmosphere.glsl"
#include "lighting.glsl"
#include "noise.glsl"
#include "sky.glsl"
#include "water.glsl"

layout(push_constant) uniform PushConstants
{
    vec4 surface; // x: the surface's height
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2  onPicture = gl_FragCoord.xy / vec2(textureSize(sceneColor, 0));
    vec3  camera    = sceneLights.cameraPosition.xyz;
    float distance  = min(GetSolidViewDepth(onPicture), NothingBehind);
    vec3  kept      = exp(-Absorption * distance);
    vec3  glow      = GetInsideGlow(camera, 0.0, push.surface.x - camera.y);
    vec3  seen      = texture(sceneColor, onPicture).rgb;
    outColor        = vec4(kept * seen + (1.0 - kept) * glow + GetDither(gl_FragCoord.xy) * ColorStep, 1.0);
}
