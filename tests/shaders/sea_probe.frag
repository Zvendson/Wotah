#version 450
#extension GL_GOOGLE_include_directive : require

// The sea as the GPU computes it (sea_waves.glsl, with the floor: terrain_shape.glsl), for comparing with the CPU's
// (tests/gpu_tests.cpp): the pixel at (column, row) holds one number of the place ProbeCorner + (column, row) *
// ProbeSpacing, every wave kept: the wind waves' offset of the water at rest there, x, y or z (push.part.x 0, 1, 2),
// their share (3), the swell's surface over the still level on the sand there (4), or how wet the sand is (5). Its 32
// bits are spread over the four 8-bit channels (red the lowest), so they come back exactly. Drawn with fullscreen.vert.

#define WOTAH_TERRAIN_SHAPE_SET     3
#define WOTAH_TERRAIN_SHAPE_BINDING 1

#include "noise.glsl"
#include "terrain_shape.glsl"
#include "sea_waves.glsl"

layout(push_constant) uniform PushConstants
{
    uvec4 part; // x: which number to write
} push;

layout(location = 0) out vec4 outColor;

// Where the probed grid starts and how far apart its places are, in metres; the test uses the same.
const vec2  ProbeCorner  = vec2(-150.0, -150.0);
const float ProbeSpacing = 0.5;

void main()
{
    vec2  place = ProbeCorner + floor(gl_FragCoord.xy) * ProbeSpacing;
    float value = 0.0;
    if (push.part.x < 4u)
    {
        float shoal = GetShoalShare(place);
        vec3  tilt;
        float lost;
        vec3  offset = GetWaveOffset(place, shoal, 0.0, tilt, lost);
        value        = push.part.x < 3u ? offset[push.part.x] : shoal;
    }
    else if (push.part.x == 4u)
    {
        vec2      frame = GetSurfFrame(place);
        float     clock = seaWaves.swellClock.x;
        SurfPoint point = GetSurfPoint(frame.y, frame.x, clock);
        float     sheet;
        float     foam;
        float     behind;
        value = GetSurfaceFrom(frame, point, clock, GetTerrainHeight(place, 0.0), 0.0, sheet, foam, behind) -
                seaWaves.shoal.w;
    }
    else
    {
        float trace;
        value = GetSandWetness(place, trace);
    }
    uint  bits  = floatBitsToUint(value);
    uvec4 bytes = uvec4(bits, bits >> 8u, bits >> 16u, bits >> 24u) & 255u;
    outColor    = vec4(bytes) / 255.0;
}
