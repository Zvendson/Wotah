#version 450

// A test shader that uses every input a pipeline can give: vertex attributes, push constants and a uniform buffer.
// It moves the vertices like a wave, so the inputs are really read (unused inputs would be dropped by the compiler).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform Push
{
    mat4  viewProjection;
    float time;
} push;

layout(set = 0, binding = 0) uniform Wave
{
    vec4 amplitudeFrequency; // x = amplitude, y = frequency
} wave;

layout(location = 0) out vec3 outNormal;

void main()
{
    vec3 position = inPosition;
    position.y += wave.amplitudeFrequency.x * sin(push.time * wave.amplitudeFrequency.y + position.x);
    outNormal   = inNormal;
    gl_Position = push.viewProjection * vec4(position, 1.0);
}
