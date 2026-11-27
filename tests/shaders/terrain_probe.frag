#version 450
#extension GL_GOOGLE_include_directive : require

// The land's height as the GPU computes it (terrain_shape.glsl), for comparing with the CPU's (tests/gpu_tests.cpp):
// the pixel at (column, row) holds the height at ProbeCorner + (column, row) * ProbeSpacing, its 32 bits spread over
// the four 8-bit channels (red the lowest), so they come back exactly. Drawn with fullscreen.vert.

#include "noise.glsl"
#include "terrain_shape.glsl"

layout(location = 0) in vec2 inScreenPosition;

layout(location = 0) out vec4 outColor;

// Where the probed grid starts and how far apart its places are, in metres; the test uses the same.
const vec2  ProbeCorner  = vec2(-150.0, -150.0);
const float ProbeSpacing = 0.5;

void main()
{
    vec2  place  = ProbeCorner + floor(gl_FragCoord.xy) * ProbeSpacing;
    uint  bits   = floatBitsToUint(GetTerrainHeight(place, 0.0));
    uvec4 bytes  = uvec4(bits, bits >> 8u, bits >> 16u, bits >> 24u) & 255u;
    outColor     = vec4(bytes) / 255.0;
}
