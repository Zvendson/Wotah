// Placing a vertex of a geometry clipmap (#include "clipmap.glsl"; source/render/clipmap.hpp): the land's and the
// water's vertex shaders share it.

#ifndef WOTAH_CLIPMAP_GLSL
#define WOTAH_CLIPMAP_GLSL

// Where a level's outer band starts, as a share of its half width: from there out, the level's vertices blend into the
// next coarser level's grid, fully at its edge, so the two meet without cracks (Strugar 2009, "Continuous Distance-
// Dependent Level of Detail for Rendering Heightmaps": morphing). A quarter of the level is wide enough to hide it.
const float ClipmapMorphStart = 0.75;

// Where a vertex of the clipmap's grid lands in the world: xy its x and z, z how far it has blended into the next
// coarser level (0 to 1). gridPlace is the vertex's place on the grid in cells from the level's middle (whole numbers);
// level holds the level's middle (xy), its cells' size (z) and half its width in cells (w).
vec3 PlaceClipmapVertex(vec2 gridPlace, vec4 level)
{
    // How far out the vertex lies, 1 at the level's square edge, and how far it has blended there.
    float outwards = max(abs(gridPlace.x), abs(gridPlace.y)) / level.w;
    float morph    = clamp((outwards - ClipmapMorphStart) / (1.0 - ClipmapMorphStart), 0.0, 1.0);

    // The coarser level's vertices lie on every second line of this level's grid (even cells); a vertex on an odd line
    // slides onto the even one below it as the blend grows, so at the edge every vertex lies on the coarser grid.
    vec2 odd     = gridPlace - 2.0 * floor(gridPlace * 0.5);
    vec2 blended = gridPlace - odd * morph;
    return vec3(level.xy + blended * level.z, morph);
}

#endif // WOTAH_CLIPMAP_GLSL
