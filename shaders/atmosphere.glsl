// What sky and ground share (#include "atmosphere.glsl"): the color of the day sky at the horizon, how far things fade
// into the air's haze, and how a pixel finds the direction it looks in.

const vec3  HorizonColor = vec3(0.62, 0.78, 0.94); // the day sky at the horizon: pale, hazy blue (the haze's day color)
const float HazeDensity  = 0.012; // per world unit: a quarter faded at 24 units, half at 58 (the ground is 64 across)

// A color seen through distance units of air: it fades into the haze's color (exponential fog). The haze follows the
// time of day (SceneLights::HazeColor): HorizonColor by day, so the ground meets the sky, darker at dusk and night.
vec3 ApplyHaze(vec3 color, float distance, vec3 hazeColor)
{
    return mix(color, hazeColor, 1.0 - exp(-distance * HazeDensity));
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
