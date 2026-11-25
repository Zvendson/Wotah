#version 450
#extension GL_GOOGLE_include_directive : require

// The ground (source/render/height_field.hpp): grass, lit like everything else, with a faint checker of 1-unit tiles
// so movement shows, fading into the horizon's haze with distance.

#include "atmosphere.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 1) in vec3  inLocalPosition; // the ground is not moved, so this is the world position
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

layout(location = 0) out vec4 outColor;

const vec3  GrassColor   = vec3(0.30, 0.45, 0.20); // muted green
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
    vec3  grass     = GrassColor * (1.0 + (parity * 2.0 - 1.0) * contrast);

    outColor = vec4(ApplyHaze(Light(grass, inWorldNormal, inWorldPosition, inViewDepth), inViewDepth), 1.0);
}
