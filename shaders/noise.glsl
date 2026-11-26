// Smooth randomness for landscapes and other natural shapes (#include "noise.glsl"): a hash, gradient noise with its
// slope, and layered noise, plain and eroded. Every function mirrors its twin in source/core/utils.cpp line by line, so
// the CPU (physics, placing things) and the GPU (drawing) see the same world; a change to one must go into the other
// (tests/gpu_tests.cpp compares them).

#ifndef WOTAH_NOISE_GLSL
#define WOTAH_NOISE_GLSL

// Three well mixed numbers from three whole numbers, the same every time: the pcg3d hash (Jarzynski and Olano 2020,
// "Hash Functions for GPU Rendering").
uvec3 Hash(uvec3 value)
{
    value = value * 1664525u + 1013904223u;
    value.x += value.y * value.z;
    value.y += value.z * value.x;
    value.z += value.x * value.y;
    value ^= value >> 16u;
    value.x += value.y * value.z;
    value.y += value.z * value.x;
    value.z += value.x * value.y;
    return value;
}

// Three numbers from 0 to 1 that belong to one cell of a grid, the same every time.
vec3 GetCellRandom(vec3 cell, uint salt)
{
    return vec3(Hash(uvec3(ivec3(cell)) + salt)) / 4294967295.0;
}

// The gradient at a whole-number corner of the noise's grid: each part from -1 up to 1. A hashed number's top 24 bits
// as a float from 0 up to 2 are exact on CPU and GPU alike (floats hold 24 bits; 2 / 2^24 is a power of two).
vec2 GetCornerGradient(ivec2 corner, uint seed)
{
    uvec3 mixed = Hash(uvec3(uvec2(corner), seed));
    return vec2(mixed.xy >> 8u) * (2.0 / 16777216.0) - 1.0;
}

// Gradient noise (Perlin's kind; Perlin 2002, "Improving Noise") over a plane, with its slope: x is the value, about
// -1 to 1 and 0 at every place with whole-number coordinates; y and z how fast it rises along the place's x and y.
vec3 GetGradientNoise(vec2 place, uint seed)
{
    // The grid cell the place lies in, and where in it (0 to 1 on each side).
    vec2  cell   = floor(place);
    vec2  f      = place - cell;
    ivec2 corner = ivec2(cell);

    // The gradients at the cell's four corners, and each one's value at the place: how far the place lies along it.
    vec2  a  = GetCornerGradient(corner, seed);
    vec2  b  = GetCornerGradient(corner + ivec2(1, 0), seed);
    vec2  c  = GetCornerGradient(corner + ivec2(0, 1), seed);
    vec2  d  = GetCornerGradient(corner + ivec2(1, 1), seed);
    float va = dot(a, f);
    float vb = dot(b, f - vec2(1.0, 0.0));
    float vc = dot(c, f - vec2(0.0, 1.0));
    float vd = dot(d, f - vec2(1.0, 1.0));

    // Blended with the quintic fade 6t^5 - 15t^4 + 10t^3, smooth to the second slope, and that fade's slope
    // 30t^2 (t - 1)^2 for the noise's own slope (Quilez, "Gradient Noise Derivatives", iquilezles.org).
    vec2  fade      = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2  fadeSlope = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float twist     = va - vb - vc + vd;
    float value     = va + fade.x * (vb - va) + fade.y * (vc - va) + fade.x * fade.y * twist;
    vec2  slope     = a + fade.x * (b - a) + fade.y * (c - a) + fade.x * fade.y * (a - b - c + d) +
                  fadeSlope * (vec2(vb - va, vc - va) + fade.yx * twist);
    return vec3(value, slope);
}

// Between layers of noise: turned by about 37 degrees (cos 0.8, sin 0.6, both exact in binary floats) and twice as
// fine, so the layers' grids never line up (Quilez, "fBm", iquilezles.org); each half as strong.
vec2 ToNextLayer(vec2 place)
{
    return vec2(0.8 * place.x - 0.6 * place.y, 0.6 * place.x + 0.8 * place.y) * 2.0;
}

// How much of a layer with this wavelength is kept when detail below smallest is left out: none below it, all from
// twice it, smoothly between; all of every layer when smallest is 0 or less.
float GetLayerShare(float wavelength, float smallest)
{
    if (!(smallest > 0.0))
    {
        return 1.0;
    }
    return smoothstep(smallest, 2.0 * smallest, wavelength);
}

// Layered noise (fractional Brownian motion): layers of gradient noise, each twice as fine and half as strong, about
// -1 to 1, 0 at the origin. Layers with a wavelength (the first one's is 1) below smallest are left out, those between
// smallest and twice that faded, so asking for less detail changes the result smoothly.
float GetLayeredNoise(vec2 place, uint seed, int layers, float smallest)
{
    float sum        = 0.0;
    float total      = 0.0;
    float strength   = 1.0;
    float wavelength = 1.0;
    vec2  at         = place;
    for (int layer = 0; layer < layers; ++layer)
    {
        float kept = GetLayerShare(wavelength, smallest);
        sum += strength * kept * GetGradientNoise(at, seed + uint(layer)).x;
        total += strength;
        strength *= 0.5;
        wavelength *= 0.5;
        at = ToNextLayer(at);
    }
    return total > 0.0 ? sum / total : 0.0;
}

// Layered noise as water and weather wear land down: each layer counts less where the layers before it are steep, so
// slopes stay smooth and the detail gathers in valleys and on flat tops (Quilez, "Terrain Raymarching",
// iquilezles.org). About -1 to 1, 0 at the origin.
float GetErodedNoise(vec2 place, uint seed, int layers, float smallest)
{
    float sum        = 0.0;
    float total      = 0.0;
    float strength   = 1.0;
    float wavelength = 1.0;
    vec2  steepness  = vec2(0.0);
    vec2  at         = place;
    for (int layer = 0; layer < layers; ++layer)
    {
        // Each layer counts less the steeper the layers so far are: 1 / (1 + slope^2).
        float kept  = GetLayerShare(wavelength, smallest);
        vec3  noise = GetGradientNoise(at, seed + uint(layer));
        steepness += kept * noise.yz;
        sum += strength * kept * noise.x / (1.0 + dot(steepness, steepness));
        total += strength;
        strength *= 0.5;
        wavelength *= 0.5;
        at = ToNextLayer(at);
    }
    return total > 0.0 ? sum / total : 0.0;
}

#endif // WOTAH_NOISE_GLSL
