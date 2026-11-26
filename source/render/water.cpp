/**
 * @file water.cpp
 * Where the sea's surface is.
 */
#include "render/water.hpp"

#include <cmath>

std::optional<float> Wotah::FindWaterHeight(const Sea& sea, float x, float z)
{
    if (!std::isfinite(x) || !std::isfinite(z))
    {
        return std::nullopt;
    }
    return sea.Level;
}
