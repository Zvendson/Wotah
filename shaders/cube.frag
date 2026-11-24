#version 450

// Orange, lit by one fixed light from above, so every face of the cube has its own brightness.

layout(location = 0) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

const vec3  BaseColor      = vec3(1.0, 0.5, 0.0);           // orange
const vec3  LightDirection = normalize(vec3(0.4, 1.0, 0.25)); // towards the light: up, a bit right and front
const float Ambient        = 0.25;                           // light that reaches faces turned away

void main()
{
    float diffuse = max(dot(normalize(inNormal), LightDirection), 0.0);
    outColor      = vec4(BaseColor * (Ambient + (1.0 - Ambient) * diffuse), 1.0);
}
