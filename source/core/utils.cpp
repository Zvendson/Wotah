/**
 * @file utils.cpp
 * The reusable helpers declared in utils.hpp. The noise functions mirror shaders/noise.glsl line by line; a change
 * to one must go into the other (tests/gpu_tests.cpp compares them).
 */
#include "core/utils.hpp"

#ifdef _WIN32
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <string>
#include <system_error>

namespace
{
#ifdef _WIN32
    /** The first buffer size for GetModuleFileNameW: MAX_PATH covers most paths. */
    constexpr DWORD FirstPathBufferSize = MAX_PATH;

    /**
     * The retry's buffer size: the longest path Windows allows (32 767 UTF-16 units, "Maximum Path Length
     * Limitation" in the Win32 docs) plus the terminating zero.
     */
    constexpr DWORD MaxPathBufferSize = 32768;

    /** The executable's full path, from GetModuleFileNameW; empty on failure. */
    std::filesystem::path FindExecutablePath()
    {
        // GetModuleFileNameW cuts the path short (and returns the buffer size) when the buffer is too small, so a
        // too-long path is read again into the largest buffer a path can need.
        std::wstring buffer;
        for (const DWORD size : {FirstPathBufferSize, MaxPathBufferSize})
        {
            buffer.resize(size);
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), size);
            if (length == 0)
            {
                return {};
            }
            if (length < size)
            {
                buffer.resize(length);
                return buffer;
            }
        }
        return {};
    }
#else
    /** The executable's full path, from /proc/self/exe; empty on failure. */
    std::filesystem::path FindExecutablePath()
    {
        // Linux keeps a link to the running executable at /proc/self/exe (proc(5)).
        std::error_code       error;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        if (error)
        {
            return {};
        }
        return path;
    }
#endif

    // ---- Noise ----

    /** The pcg3d hash's multiplier and increment (Jarzynski and Olano 2020, "Hash Functions for GPU Rendering"). */
    constexpr uint32_t HashMultiplier = 1664525u;
    constexpr uint32_t HashIncrement  = 1013904223u;
    constexpr uint32_t HashShift      = 16u;

    /** Three well mixed numbers from three whole numbers: pcg3d, with unsigned arithmetic wrapping as on the GPU. */
    glm::uvec3 Hash(glm::uvec3 value) noexcept
    {
        value = value * HashMultiplier + HashIncrement;
        value.x += value.y * value.z;
        value.y += value.z * value.x;
        value.z += value.x * value.y;
        value ^= value >> HashShift;
        value.x += value.y * value.z;
        value.y += value.z * value.x;
        value.z += value.x * value.y;
        return value;
    }

    /**
     * A hashed number's top 24 bits as a float from 0 up to 2, exact on CPU and GPU alike (floats hold 24 bits): the
     * shift drops the 8 low bits, and the factor 2 / 2^24 is a power of two.
     */
    constexpr uint32_t DroppedBits = 8u;
    constexpr float    ToZeroToTwo = 2.0f / 16777216.0f;

    /** The gradient at a whole-number corner of the noise's grid: each part from -1 up to 1. */
    glm::vec2 GetCornerGradient(const glm::ivec2& corner, uint32_t seed) noexcept
    {
        const glm::uvec3 mixed =
            Hash(glm::uvec3(static_cast<uint32_t>(corner.x), static_cast<uint32_t>(corner.y), seed));
        return glm::vec2(static_cast<float>(mixed.x >> DroppedBits), static_cast<float>(mixed.y >> DroppedBits)) *
                   ToZeroToTwo -
               1.0f;
    }

    /**
     * Between layers of noise: turned by about 37 degrees (cos 0.8, sin 0.6, both exact in binary floats) and twice as
     * fine, so the layers' grids never line up (Quilez, "fBm", iquilezles.org).
     */
    constexpr float LayerCos   = 0.8f;
    constexpr float LayerSin   = 0.6f;
    constexpr float LayerScale = 2.0f;
    constexpr float LayerGain  = 0.5f;

    glm::vec2 ToNextLayer(const glm::vec2& place) noexcept
    {
        return glm::vec2(LayerCos * place.x - LayerSin * place.y, LayerSin * place.x + LayerCos * place.y) * LayerScale;
    }
} // namespace

bool Wotah::IsFinite(const glm::vec3& vector) noexcept
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

std::optional<std::filesystem::path> Wotah::FindExecutableDirectory()
{
    const std::filesystem::path path = FindExecutablePath();
    if (path.empty() || !path.has_parent_path())
    {
        return std::nullopt;
    }
    return path.parent_path();
}

float Wotah::GetLayerShare(float wavelength, float smallest) noexcept
{
    if (!(smallest > 0.0f))
    {
        return 1.0f;
    }
    return glm::smoothstep(smallest, 2.0f * smallest, wavelength);
}

glm::vec3 Wotah::GetGradientNoise(const glm::vec2& place, uint32_t seed) noexcept
{
    // The grid cell the place lies in, and where in it (0 to 1 on each side).
    const glm::vec2  cell = glm::floor(place);
    const glm::vec2  f    = place - cell;
    const glm::ivec2 corner(cell);

    // The gradients at the cell's four corners, and each one's value at the place: how far the place lies along it.
    const glm::vec2 a  = GetCornerGradient(corner, seed);
    const glm::vec2 b  = GetCornerGradient(corner + glm::ivec2(1, 0), seed);
    const glm::vec2 c  = GetCornerGradient(corner + glm::ivec2(0, 1), seed);
    const glm::vec2 d  = GetCornerGradient(corner + glm::ivec2(1, 1), seed);
    const float     va = glm::dot(a, f);
    const float     vb = glm::dot(b, f - glm::vec2(1.0f, 0.0f));
    const float     vc = glm::dot(c, f - glm::vec2(0.0f, 1.0f));
    const float     vd = glm::dot(d, f - glm::vec2(1.0f, 1.0f));

    // Blended with the quintic fade 6t^5 - 15t^4 + 10t^3, smooth to the second slope, and that fade's slope
    // 30t^2 (t - 1)^2 for the noise's own slope (Quilez, "Gradient Noise Derivatives", iquilezles.org).
    const glm::vec2 fade      = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const glm::vec2 fadeSlope = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
    const float     twist     = va - vb - vc + vd;
    const float     value     = va + fade.x * (vb - va) + fade.y * (vc - va) + fade.x * fade.y * twist;
    const glm::vec2 slope     = a + fade.x * (b - a) + fade.y * (c - a) + fade.x * fade.y * (a - b - c + d) +
                                fadeSlope * (glm::vec2(vb - va, vc - va) + glm::vec2(fade.y, fade.x) * twist);
    return {value, slope.x, slope.y};
}

float Wotah::GetLayeredNoise(const glm::vec2& place, uint32_t seed, int layers, float smallest) noexcept
{
    float     sum        = 0.0f;
    float     total      = 0.0f;
    float     strength   = 1.0f;
    float     wavelength = 1.0f;
    glm::vec2 at         = place;
    for (int layer = 0; layer < layers; ++layer)
    {
        const float kept = GetLayerShare(wavelength, smallest);
        sum += strength * kept * GetGradientNoise(at, seed + static_cast<uint32_t>(layer)).x;
        total += strength;
        strength *= LayerGain;
        wavelength *= LayerGain;
        at = ToNextLayer(at);
    }
    return total > 0.0f ? sum / total : 0.0f;
}

float Wotah::GetErodedNoise(const glm::vec2& place, uint32_t seed, int layers, float smallest) noexcept
{
    float     sum        = 0.0f;
    float     total      = 0.0f;
    float     strength   = 1.0f;
    float     wavelength = 1.0f;
    glm::vec2 steepness{0.0f};
    glm::vec2 at = place;
    for (int layer = 0; layer < layers; ++layer)
    {
        // Each layer counts less the steeper the layers so far are: 1 / (1 + slope^2).
        const float     kept  = GetLayerShare(wavelength, smallest);
        const glm::vec3 noise = GetGradientNoise(at, seed + static_cast<uint32_t>(layer));
        steepness += kept * glm::vec2(noise.y, noise.z);
        sum += strength * kept * noise.x / (1.0f + glm::dot(steepness, steepness));
        total += strength;
        strength *= LayerGain;
        wavelength *= LayerGain;
        at = ToNextLayer(at);
    }
    return total > 0.0f ? sum / total : 0.0f;
}
