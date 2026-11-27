#version 450

// Every mesh (source/render/mesh_data.hpp): places the vertex in the world with the object's matrix, then sees it
// through the camera. Hands on the world normal and position for lighting and shadows, the mesh's own position and
// normal for effects that belong to the shape itself (the cube's edges, the ground's tiles), and the distance from the
// camera for the haze.

layout(location = 0) in vec3 inPosition; // in the mesh's own space
layout(location = 1) in vec3 inNormal;   // the surface's outward direction there

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: the camera and its perspective
    mat4 model;          // the mesh's own space to world space: where the object is (no non-uniform scaling)
} push;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outLocalPosition;
layout(location = 2) out vec3 outLocalNormal;
layout(location = 3) out float outViewDepth;    // how far in front of the camera, for the haze and the shadows
layout(location = 4) out vec3  outWorldPosition; // where the point is in the world, for the shadows

void main()
{
    // With rotation, translation and uniform scaling only, the model matrix turns normals the same way it turns
    // directions; normalize in the fragment shader takes care of the scale.
    outWorldNormal   = mat3(push.model) * inNormal;
    outLocalPosition = inPosition;
    outLocalNormal   = inNormal;
    outWorldPosition = (push.model * vec4(inPosition, 1.0)).xyz;
    gl_Position      = push.viewProjection * vec4(outWorldPosition, 1.0);

    // With a perspective projection, clip space's w is the distance along the camera's view direction.
    outViewDepth = gl_Position.w;
}
