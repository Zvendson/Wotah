#version 450
#extension GL_GOOGLE_include_directive : require

// The ball: blue and lit (lighting.glsl), smooth because its normals follow the sphere.

#include "lighting.glsl"

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

const vec3 BaseColor = vec3(0.2, 0.45, 1.0); // blue, which stands out best against the orange cube

void main()
{
    outColor = vec4(Light(BaseColor, inWorldNormal), 1.0);
}
