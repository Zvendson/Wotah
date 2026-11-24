// The scene's lights, shared by every fragment shader (#include "lighting.glsl"): an ambient color that reaches every
// surface, and up to WOTAH_MAX_DIRECTIONAL_LIGHTS directional lights (the sun, the moon), each with its own color.
// Written every frame by source/render/lighting.cpp; WOTAH_MAX_DIRECTIONAL_LIGHTS comes from CMakeLists.txt, the same
// number the C++ side uses.

// One directional light; matches GpuDirectionalLight in source/render/lighting.cpp (std140: two vec4s).
struct DirectionalLight
{
    vec4 towardsLight; // xyz: towards the light, length 1
    vec4 radiance;     // rgb: color times intensity
};

layout(std140, set = 0, binding = 0) uniform SceneLights
{
    vec4             ambient; // rgb: the light reaching every surface
    uvec4            count;   // x: how many directional lights are used
    DirectionalLight directional[WOTAH_MAX_DIRECTIONAL_LIGHTS];
} sceneLights;

// A color lit by the scene, for a surface facing along normal (any length): the ambient light, plus every
// directional light by how squarely it falls on the surface (Lambert).
vec3 Light(vec3 color, vec3 normal)
{
    vec3 surface  = normalize(normal);
    vec3 received = sceneLights.ambient.rgb;
    uint used     = min(sceneLights.count.x, uint(WOTAH_MAX_DIRECTIONAL_LIGHTS));
    for (uint index = 0; index < used; ++index)
    {
        DirectionalLight light = sceneLights.directional[index];
        received += light.radiance.rgb * max(dot(surface, light.towardsLight.xyz), 0.0);
    }
    return color * received;
}
