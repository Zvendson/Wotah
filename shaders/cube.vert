#version 450

// The cube from its vertex buffer (source/render/cube_geometry.hpp): size 1 around the origin, every face wound
// counter-clockwise when seen from outside (its front side), so back faces can be culled.

layout(location = 0) in vec3 inPosition; // -0.5 .. 0.5 per axis
layout(location = 1) in vec3 inNormal;   // the face's outward direction

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: camera and perspective
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outCubePosition; // the point on the cube, for the black edges

void main()
{
    outNormal       = inNormal;
    outCubePosition = inPosition;
    gl_Position     = push.viewProjection * vec4(inPosition, 1.0);
}
