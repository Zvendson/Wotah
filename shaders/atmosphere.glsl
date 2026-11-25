// What sky and ground share (#include "atmosphere.glsl"): the color of the air at the horizon, where the sky ends and
// far things fade into it, and how a pixel finds the direction it looks in.

const vec3  HorizonColor = vec3(0.62, 0.78, 0.94); // pale, hazy blue
const float HazeDensity  = 0.012; // per world unit: a quarter faded at 24 units, half at 58 (the ground is 64 across)

// A color seen through distance units of air: it fades into the horizon's haze (exponential fog).
vec3 ApplyHaze(vec3 color, float distance)
{
    return mix(color, HorizonColor, 1.0 - exp(-distance * HazeDensity));
}

// The world-space points where the pixel's view ray crosses the camera's near and far plane, from the inverse of the
// camera's view-projection; the ray runs from the first to the second.
void GetViewRay(mat4 inverseViewProjection, vec2 screenPosition, out vec3 nearPoint, out vec3 farPoint)
{
    vec4 nearClip = inverseViewProjection * vec4(screenPosition, 0.0, 1.0);
    vec4 farClip  = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    nearPoint     = nearClip.xyz / nearClip.w;
    farPoint      = farClip.xyz / farClip.w;
}
