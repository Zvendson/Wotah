// Noise that repeats (#include "tile_noise.glsl", after noise.glsl), for textures that tile seamlessly: gradient noise
// and layered noise whose grids wrap every period cells, and random numbers per cell of a wrapping grid. Used on the
// GPU only, to make the ground's textures (terrain_material.comp), so it has no twin on the CPU.

#ifndef WOTAH_TILE_NOISE_GLSL
#define WOTAH_TILE_NOISE_GLSL

// A whole-number corner wrapped into 0 up to period - 1 on each side (period above 0).
ivec2 WrapCorner(ivec2 corner, int period)
{
    return ((corner % period) + period) % period;
}

// Gradient noise as noise.glsl's GetGradientNoise (value, slope along x and y), repeating every period cells: the
// corners' gradients wrap, so the noise at place and at place + period are the same.
vec3 GetTileNoise(vec2 place, int period, uint seed)
{
    vec2  cell   = floor(place);
    vec2  f      = place - cell;
    ivec2 corner = ivec2(cell);

    vec2  a  = GetCornerGradient(WrapCorner(corner, period), seed);
    vec2  b  = GetCornerGradient(WrapCorner(corner + ivec2(1, 0), period), seed);
    vec2  c  = GetCornerGradient(WrapCorner(corner + ivec2(0, 1), period), seed);
    vec2  d  = GetCornerGradient(WrapCorner(corner + ivec2(1, 1), period), seed);
    float va = dot(a, f);
    float vb = dot(b, f - vec2(1.0, 0.0));
    float vc = dot(c, f - vec2(0.0, 1.0));
    float vd = dot(d, f - vec2(1.0, 1.0));

    vec2  fade      = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2  fadeSlope = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float twist     = va - vb - vc + vd;
    float value     = va + fade.x * (vb - va) + fade.y * (vc - va) + fade.x * fade.y * twist;
    vec2  slope     = a + fade.x * (b - a) + fade.y * (c - a) + fade.x * fade.y * (a - b - c + d) +
                  fadeSlope * (vec2(vb - va, vc - va) + fade.yx * twist);
    return vec3(value, slope);
}

// Layered noise as noise.glsl's GetLayeredNoise, repeating every period cells of its first layer: each layer twice as
// fine (with twice the period, so it still repeats; not turned, which would break the repeat) and half as strong.
// Layers with a wavelength (the first one's is 1) below smallest are left out, those up to twice that faded.
float GetTileLayeredNoise(vec2 place, int period, uint seed, int layers, float smallest)
{
    float sum        = 0.0;
    float total      = 0.0;
    float strength   = 1.0;
    float wavelength = 1.0;
    for (int layer = 0; layer < layers; ++layer)
    {
        sum += strength * GetLayerShare(wavelength, smallest) * GetTileNoise(place, period, seed + uint(layer)).x;
        total += strength;
        strength *= 0.5;
        wavelength *= 0.5;
        place *= 2.0;
        period *= 2;
    }
    return total > 0.0 ? sum / total : 0.0;
}

// Three numbers from 0 to 1 for a cell of a grid that repeats every period cells.
vec3 GetTileCellRandom(ivec2 cell, int period, uint salt)
{
    return GetCellRandom(vec3(WrapCorner(cell, period), 0.0), salt);
}

#endif // WOTAH_TILE_NOISE_GLSL
