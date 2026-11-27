#version 450
#extension GL_GOOGLE_include_directive : require

// A simple ground for the tests (tests/gpu_tests.cpp): a flat grid (MakeTerrainMesh) drawn with mesh.vert, grass, and
// sand where it lies under the water (below WaterLevel), lit like everything else, with a faint checker of 1-unit
// tiles, fading into the air's haze with distance. The program's own land is shaders/terrain.frag; this one keeps the
// light, shadow and haze tests on a plain surface whose colors they know.

#include "atmosphere.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 1) in vec3  inLocalPosition; // the ground is not moved, so this is the world position
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

layout(location = 0) out vec4 outColor;

const vec3  GrassColor   = vec3(0.30, 0.45, 0.20); // muted green
const vec3  SandColor    = vec3(0.62, 0.55, 0.40); // pale, a little warm, wet: by eye
const float WaterLevel   = 0.0;  // below this height the ground is sand
const float ShoreBlend   = 0.1;  // grass turns to sand over this much height below the water's level
const float TileSize     = 1.0;  // the checker's squares, in world units
const float TileContrast = 0.06; // how much lighter and darker the squares are

void main()
{
    // The checker: squares alternately lighter and darker. Where one pixel covers several squares (far away) the
    // pattern fades out instead of flickering.
    vec2  tile      = inLocalPosition.xz / TileSize;
    float parity    = mod(floor(tile.x) + floor(tile.y), 2.0);
    float footprint = max(fwidth(tile.x), fwidth(tile.y));
    float contrast  = TileContrast * clamp(1.0 - footprint * 2.0, 0.0, 1.0);
    float wet       = 1.0 - smoothstep(WaterLevel - ShoreBlend, WaterLevel, inWorldPosition.y);
    vec3  ground    = mix(GrassColor, SandColor, wet) * (1.0 + (parity * 2.0 - 1.0) * contrast);

    vec3 lit = Light(ground, inWorldNormal, inWorldPosition, inViewDepth);
    outColor = vec4(ApplyHaze(lit, inViewDepth, sceneLights.hazeColor.rgb), 1.0);
}
