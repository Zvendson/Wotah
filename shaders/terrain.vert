#version 450
#extension GL_GOOGLE_include_directive : require

// The land (source/render/terrain_renderer.hpp): every vertex of the clipmap's grid placed in the world around the
// viewer (clipmap.glsl) and lifted to the land's height (terrain_shape.glsl), with the land's normal from its
// neighbours' heights. Each level keeps only detail its vertices can show, so far land stays calm; where a level blends
// into the next, it blends its detail too, so both compute the very same heights where they meet and no crack opens.

#include "noise.glsl"
#include "terrain_shape.glsl"
#include "clipmap.glsl"

layout(location = 0) in vec3 inGridPlace; // x, z: the vertex's place on the grid, in cells from the level's middle

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: the camera and its perspective
    vec4 level;          // xy: the level's middle; z: its cells' size; w: half its width in cells
} push;

layout(location = 0) out vec3  outWorldNormal;
layout(location = 3) out float outViewDepth;     // how far in front of the camera, for the haze and the shadows
layout(location = 4) out vec3  outWorldPosition; // where the point is in the world, for lighting and the layers

// The finest detail a level keeps, in its vertex spacings: a wavelength needs at least two vertices to show, and
// GetLayerShare keeps it whole only from twice that, so nothing shorter than four spacings flickers as the grid moves.
const float DetailSpacings = 2.0;

void main()
{
    vec3  placed = PlaceClipmapVertex(inGridPlace.xz, push.level);
    vec2  place  = placed.xy;
    float morph  = placed.z;

    // The spacing the vertex stands for: its level's, growing into the next coarser level's at the blended edge.
    float spacing  = push.level.z * (1.0 + morph);
    float smallest = DetailSpacings * spacing;
    float height   = GetTerrainHeight(place, smallest);

    // The normal from the heights one spacing to each side (central differences): rises along x and z.
    float east  = GetTerrainHeight(place + vec2(spacing, 0.0), smallest);
    float west  = GetTerrainHeight(place - vec2(spacing, 0.0), smallest);
    float south = GetTerrainHeight(place + vec2(0.0, spacing), smallest);
    float north = GetTerrainHeight(place - vec2(0.0, spacing), smallest);
    outWorldNormal = normalize(vec3(west - east, 2.0 * spacing, north - south));

    outWorldPosition = vec3(place.x, height, place.y);
    gl_Position      = push.viewProjection * vec4(outWorldPosition, 1.0);

    // With a perspective projection, clip space's w is the distance along the camera's view direction.
    outViewDepth = gl_Position.w;
}
