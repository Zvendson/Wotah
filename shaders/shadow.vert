#version 450

// Every shape that casts shadows, drawn into a light's shadow map (source/render/shadow_maps.hpp): only where it is,
// as the light sees it. Depth only, so there is no fragment shader. The same push constants and vertices as mesh.vert,
// with the light's view of the shadow area in place of the camera. The land has its own (terrain_shadow.vert).

layout(location = 0) in vec3 inPosition; // in the mesh's own space

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to the light's shadow map
    mat4 model;          // the mesh's own space to world space: where the object is
} push;

void main()
{
    gl_Position = push.viewProjection * push.model * vec4(inPosition, 1.0);
}
