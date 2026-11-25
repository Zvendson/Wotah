/**
 * @file height_field.hpp
 * The ground's shape: a grid of heights over a limited area of the x-z plane, centered on the origin. Hills and slopes
 * are heights; FindHeight tells how high the ground is anywhere on it (for placing things on it, later for physics),
 * and MakeTerrainMesh turns it into triangles to draw.
 *
 *     Wotah::HeightField field = Wotah::MakeFlatHeightField(64.0f, 64.0f, 128, 128);
 *     field.Heights[index] = 2.0f;                             // raise a point: a hill
 *     const std::optional<float> y = Wotah::FindHeight(field, x, z);
 *     terrain.Create(device, Wotah::MakeTerrainMesh(field));
 */
#pragma once

#include "render/mesh_data.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Wotah
{
    /**
     * Heights over a width x depth area around the origin, split into columns x rows squares. Heights holds one height
     * per grid point: (Columns + 1) x (Rows + 1) of them, row by row from -z to +z, each row from -x to +x (the same
     * order as MakeGrid's vertices).
     */
    struct HeightField
    {
        /** The area's size along x and along z, in world units. */
        float Width = 0.0f;
        float Depth = 0.0f;
        /** How many squares the area is split into along x and along z. */
        uint32_t Columns = 0;
        uint32_t Rows    = 0;
        /** The height of every grid point. */
        std::vector<float> Heights;
    };

    /**
     * A flat field at height 0. Empty (and logged) for a size that is not above 0, or for 0 columns or rows or more
     * grid points than 32-bit indices can number.
     */
    [[nodiscard]] HeightField MakeFlatHeightField(float width, float depth, uint32_t columns, uint32_t rows);

    /** The index of a grid point in HeightField::Heights; column 0..Columns, row 0..Rows. */
    [[nodiscard]] std::size_t GetHeightIndex(const HeightField& field, uint32_t column, uint32_t row) noexcept;

    /** Whether the field's sizes fit together and it has one height per grid point; logs the reason when not. */
    [[nodiscard]] bool IsUsable(const HeightField& field);

    /**
     * The ground's height at world x, z, exactly on the triangles MakeTerrainMesh draws; empty outside the area or for
     * a field that is not usable.
     */
    [[nodiscard]] std::optional<float> FindHeight(const HeightField& field, float x, float z);

    /**
     * The field as triangles facing up, with normals that follow the slopes, so hills are lit correctly. Empty (and
     * logged) for a field that is not usable.
     */
    [[nodiscard]] MeshData MakeTerrainMesh(const HeightField& field);
} // namespace Wotah
