#version 450

// A cube of size 1 around the origin, made here without a vertex buffer: 6 faces x 2 triangles x 3 corners = 36
// vertices. Every face is wound counter-clockwise when seen from outside (its front side), so back faces can be culled.

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: camera and perspective
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outCubePosition; // the point on the cube, -0.5 .. 0.5 per axis, for the black edges

// The 8 corners, numbered by bits: x = bit 0, y = bit 1, z = bit 2 (0 means -0.5, 1 means +0.5).
const vec3 Corners[8] = vec3[](
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5)
);

// Two triangles per face, counter-clockwise seen from outside, in the order of Normals.
const int Indices[36] = int[](
    1, 3, 7,  1, 7, 5,  // +x
    0, 4, 6,  0, 6, 2,  // -x
    2, 6, 7,  2, 7, 3,  // +y
    0, 1, 5,  0, 5, 4,  // -y
    4, 5, 7,  4, 7, 6,  // +z
    0, 2, 3,  0, 3, 1   // -z
);

const vec3 Normals[6] = vec3[](
    vec3( 1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0),
    vec3( 0.0,  1.0,  0.0), vec3( 0.0, -1.0,  0.0),
    vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0, -1.0)
);

void main()
{
    vec3 corner     = Corners[Indices[gl_VertexIndex]];
    outNormal       = Normals[gl_VertexIndex / 6];
    outCubePosition = corner;
    gl_Position     = push.viewProjection * vec4(corner, 1.0);
}
