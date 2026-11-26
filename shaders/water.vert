#version 450
#extension GL_GOOGLE_include_directive : require

// The sea's surface (source/render/water_renderer.hpp): every vertex of the clipmap's grid placed around the viewer
// (clipmap.glsl), at the sea's height.
//
// shortcut: flat and still. Waves would lift each vertex here, from the same formula as source/render/water.hpp's
// FindWaterHeight, keeping only the detail a level's vertices can show (as terrain.vert does), so levels still meet.

#include "clipmap.glsl"

layout(location = 0) in vec3 inGridPlace; // x, z: the vertex's place on the grid, in cells from the level's middle

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: the camera and its perspective
    vec4 level;          // xy: the level's middle; z: its cells' size; w: half its width in cells
    vec4 surface;        // x: the surface's height
} push;

layout(location = 0) out vec3  outWorldNormal;
layout(location = 3) out float outViewDepth;     // how far in front of the camera, for the haze and the depth behind
layout(location = 4) out vec3  outWorldPosition; // where the point is in the world, for lighting and the view ray

void main()
{
    vec3 placed      = PlaceClipmapVertex(inGridPlace.xz, push.level);
    outWorldPosition = vec3(placed.x, push.surface.x, placed.y);
    outWorldNormal   = vec3(0.0, 1.0, 0.0);
    gl_Position      = push.viewProjection * vec4(outWorldPosition, 1.0);

    // With a perspective projection, clip space's w is the distance along the camera's view direction.
    outViewDepth = gl_Position.w;
}
