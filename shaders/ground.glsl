// The ground's layers (#include "ground.glsl"): what terrain_material.comp makes into textures and terrain.frag
// covers the land with. Each layer is one layer of the textures (source/render/terrain_materials.hpp): its look (color
// and height) and its surface (slope and roughness), one tile of TileSize metres that repeats.

#ifndef WOTAH_GROUND_GLSL
#define WOTAH_GROUND_GLSL

// The layers, in the textures' order (and terrain.frag's weights: x, y, z, w).
const int WetSandLayer     = 0;
const int DrySandLayer     = 1;
const int GrassLayer       = 2;
const int RockLayer        = 3;
const int GroundLayerCount = 4;

// How many metres one texture covers on the ground, per layer: sand finely (2 millimetres a texel at 1024 texels),
// grass more finely still (its blades), rock coarsely. By eye.
const float GroundTileSize[GroundLayerCount] = float[](2.0, 2.0, 1.0, 3.0);

// How many metres the height channel's full range (0 to 1) stands for, per layer: ripples of a centimetre, grass of a
// few, rock's lumps of a decimetre. Slopes are worked out from it.
const float GroundHeightRange[GroundLayerCount] = float[](0.008, 0.012, 0.04, 0.15);

// Slopes are stored as slope * SlopeEncoding + 0.5 in 0 to 1, so slopes from -1 to 1 (45 degrees) fit.
const float SlopeEncoding = 0.5;

// Sand's wind ripples: this many across a tile along u (the world's x), their crests along v (the world's z, along
// the coast).
const int SandRipplesAcross = 20;

// How the tiles may be laid out on the land (terrain.frag, hex-tiling): whether a layer's pattern may be turned (not
// sand: its ripples keep the wind's direction), and how many times it repeats across u, so a tile is only shifted by
// whole repeats across it (sand's ripples stay in step; 0: shifted freely).
const bool  GroundTurns[GroundLayerCount]         = bool[](false, false, true, true);
const float GroundRepeatsAcross[GroundLayerCount] = float[](20.0, 20.0, 0.0, 0.0);

#endif // WOTAH_GROUND_GLSL
