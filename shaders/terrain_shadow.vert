#version 450
#extension GL_GOOGLE_include_directive : require

// The land drawn into a light's shadow map (source/render/shadow_maps.hpp): the same vertices at the same heights as
// terrain.vert, seen by the light instead of the camera. Depth only, so there is no fragment shader.

#include "noise.glsl"
#include "terrain_shape.glsl"
#include "clipmap.glsl"

layout(location = 0) in vec3 inGridPlace; // x, z: the vertex's place on the grid, in cells from the level's middle

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to the light's shadow map
    vec4 level;          // xy: the level's middle; z: its cells' size; w: half its width in cells
} push;

// As terrain.vert: the finest detail a level keeps, in its vertex spacings.
const float DetailSpacings = 2.0;

void main()
{
    vec3  placed   = PlaceClipmapVertex(inGridPlace.xz, push.level);
    float spacing  = push.level.z * (1.0 + placed.z);
    float height   = GetTerrainHeight(placed.xy, DetailSpacings * spacing);
    gl_Position    = push.viewProjection * vec4(placed.x, height, placed.y, 1.0);
}
