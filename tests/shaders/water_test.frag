#version 450

// A see-through color from the normal, for the blending tests.

layout(location = 0) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(normalize(inNormal) * 0.5 + 0.5, 0.6);
}
