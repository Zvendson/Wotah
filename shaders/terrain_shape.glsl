// The shape of the land (#include "terrain_shape.glsl", after noise.glsl): GetTerrainHeight, from the numbers of a
// TerrainShape (source/terrain/terrain_shape.hpp) that source/render/terrain_renderer.cpp writes into the uniform
// buffer at set 1 (or WOTAH_TERRAIN_SHAPE_SET). It mirrors source/terrain/terrain_shape.cpp line by line; a change to
// one must go into the other (tests/gpu_tests.cpp compares them).

#ifndef WOTAH_TERRAIN_SHAPE_GLSL
#define WOTAH_TERRAIN_SHAPE_GLSL

// Where the shape's buffer is bound: set 1, binding 0 for the land's shaders; a shader that has those for something
// else defines both before it includes this file (water.vert: the sea's set, binding 1).
#ifndef WOTAH_TERRAIN_SHAPE_SET
#define WOTAH_TERRAIN_SHAPE_SET 1
#endif
#ifndef WOTAH_TERRAIN_SHAPE_BINDING
#define WOTAH_TERRAIN_SHAPE_BINDING 0
#endif

// Matches GpuTerrainShape in source/render/terrain_renderer.cpp (std140: vec4s only).
layout(std140, set = WOTAH_TERRAIN_SHAPE_SET, binding = WOTAH_TERRAIN_SHAPE_BINDING) uniform TerrainShapeBlock
{
    uvec4 seed;     // x: Seed
    vec4  sea;      // x: SeaLevel, y: BeachSlope, z: BeachTop, w: SeaDepth
    vec4  coast;    // x: CoastWander, y: CoastWavelength, z: BendStrength, w: BendWavelength
    vec4  dunes;    // x: DuneStart, y: DuneRamp, z: DuneHeight
    vec4  duneSize; // x: DuneLength, y: DuneWidth
    vec4  hills;    // x: HillStart, y: HillRamp, z: HillHeight, w: HillWavelength
    vec4  bumps;    // x: BumpHeight, y: BumpWavelength, z: FinestWavelength
} terrainShape;

// How many noise layers each feature has, and the salts that give each feature noise of its own from one seed
// (terrain_shape.cpp uses the same).
const int  TerrainBendLayers  = 3;
const int  TerrainCoastLayers = 4;
const int  TerrainHillLayers  = 6;
const int  TerrainBumpLayers  = 3;
const uint TerrainBendXSalt   = 0x68E31DA4u;
const uint TerrainBendZSalt   = 0xB5297A4Du;
const uint TerrainCoastSalt   = 0x1B56C4E9u;
const uint TerrainDuneSalt    = 0x7FEB352Du;
const uint TerrainHillSalt    = 0x846CA68Bu;
const uint TerrainBumpSalt    = 0x2C1B3C6Du;

// A feature's own seed: the shape's seed spread by the golden ratio's 32-bit fraction, mixed with the feature's salt.
uint MixTerrainSeed(uint salt)
{
    return (terrainShape.seed.x * 0x9E3779B9u) ^ salt;
}

// Rises from 0 as steeply as slope and levels out at limit; 0 for a limit of 0 or less.
float Approach(float distance, float slope, float limit)
{
    if (!(limit > 0.0))
    {
        return 0.0;
    }
    return limit * (1.0 - exp(-distance * slope / limit));
}

// The beach and the sea floor by how far seaward of the waterline a place lies (negative inland).
float GetBeachProfile(float seaward)
{
    if (seaward >= 0.0)
    {
        return -Approach(seaward, terrainShape.sea.y, terrainShape.sea.w);
    }
    return Approach(-seaward, terrainShape.sea.y, terrainShape.sea.z);
}

// A place (x, z) in the coast's own frame: x along the coast (towards the south), y seaward (towards the west).
vec2 ToCoastFrame(vec2 place)
{
    return vec2(place.y, -place.x);
}

// A direction (a slope, a tilt) in the coast's frame turned back into the world's x and z: ToCoastFrame undone.
vec2 FromCoastFrame(vec2 direction)
{
    return vec2(-direction.y, direction.x);
}

// A place (x, z) in the coast's frame as the land's formula bends it (source/terrain/terrain_shape.hpp, CoastPlace):
// xy the bent place (x along the coast, y seaward), z how far seaward of the wandering waterline it lies (negative
// inland), leaving out detail finer than smallest metres.
vec3 GetCoastPlace(vec2 place, float smallest)
{
    float detail = max(smallest, terrainShape.bumps.z);
    vec2  coast  = ToCoastFrame(place);

    // Bend the land sideways a little, so coast, dunes and hills do not run in straight lines.
    vec2  bendPlace  = coast / terrainShape.coast.w;
    float bendDetail = detail / terrainShape.coast.w;
    vec2  bend = vec2(GetLayeredNoise(bendPlace, MixTerrainSeed(TerrainBendXSalt), TerrainBendLayers, bendDetail),
                      GetLayeredNoise(bendPlace, MixTerrainSeed(TerrainBendZSalt), TerrainBendLayers, bendDetail)) *
                 terrainShape.coast.z;
    vec2 bent = coast + bend;

    // The waterline wanders to and fro along the coast; how far seaward of it the place lies.
    vec2  coastPlace = vec2(bent.x / terrainShape.coast.y, 0.0);
    float shore      = terrainShape.coast.x * GetLayeredNoise(coastPlace, MixTerrainSeed(TerrainCoastSalt),
                                                              TerrainCoastLayers, detail / terrainShape.coast.y);
    return vec3(bent, bent.y - shore);
}

// How deep the still sea stands over the beach's broad profile seaward metres seaward of the waterline; negative
// inland (source/terrain/terrain_shape.cpp, GetBeachDepth).
float GetBeachDepth(float seaward)
{
    return -GetBeachProfile(seaward);
}

// The ground's height at a place (x, z), leaving out detail finer than smallest metres (0: all the shape has).
float GetTerrainHeight(vec2 place, float smallest)
{
    float detail  = max(smallest, terrainShape.bumps.z);
    vec3  frame   = GetCoastPlace(place, smallest);
    vec2  bent    = frame.xy;
    float seaward = frame.z;
    float inland  = -seaward;
    float height  = terrainShape.sea.x + GetBeachProfile(seaward);

    // Dunes behind the beach, giving way to the hills inland: long ridges along the coast, rounded on top (a crest from
    // 1 - |noise|, squared).
    float hillZone = smoothstep(terrainShape.hills.x, terrainShape.hills.x + terrainShape.hills.y, inland);
    float duneZone =
        smoothstep(terrainShape.dunes.x, terrainShape.dunes.x + terrainShape.dunes.y, inland) * (1.0 - hillZone);
    if (duneZone > 0.0)
    {
        vec2  dunePlace = vec2(bent.x / terrainShape.duneSize.x, bent.y / terrainShape.duneSize.y);
        float crest     = 1.0 - abs(GetGradientNoise(dunePlace, MixTerrainSeed(TerrainDuneSalt)).x);
        height += terrainShape.dunes.z * duneZone * crest * crest * GetLayerShare(terrainShape.duneSize.y, detail);
    }

    // Hills farther inland, worn down as water and weather would.
    if (hillZone > 0.0)
    {
        float worn = GetErodedNoise(bent / terrainShape.hills.w, MixTerrainSeed(TerrainHillSalt), TerrainHillLayers,
                                    detail / terrainShape.hills.w);
        height += terrainShape.hills.z * hillZone * (0.5 + 0.5 * worn);
    }

    // Small bumps everywhere, under the water too.
    height += terrainShape.bumps.x * GetLayeredNoise(bent / terrainShape.bumps.y, MixTerrainSeed(TerrainBumpSalt),
                                                     TerrainBumpLayers, detail / terrainShape.bumps.y);
    return height;
}

#endif // WOTAH_TERRAIN_SHAPE_GLSL
