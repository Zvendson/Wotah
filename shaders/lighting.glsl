// The scene's one light, shared by every fragment shader (#include "lighting.glsl"): a fixed light from above, so every
// face turned a different way gets its own brightness.

const vec3  LightDirection = normalize(vec3(0.4, 1.0, 0.25)); // towards the light: up, a bit right and front
const float AmbientLight   = 0.25;                           // light that reaches surfaces turned away

// A color lit by the light, for a surface facing along normal (any length).
vec3 Light(vec3 color, vec3 normal)
{
    float diffuse = max(dot(normalize(normal), LightDirection), 0.0);
    return color * (AmbientLight + (1.0 - AmbientLight) * diffuse);
}
