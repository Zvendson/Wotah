#version 450
#extension GL_GOOGLE_include_directive : require

// The sea's surface (source/render/water_renderer.hpp): every vertex of the clipmap's grid placed around the viewer
// (clipmap.glsl) at the sea's height, then moved by the wind's waves (shrunk over shallow ground as the floor's depth
// lets them stand) and lifted by the swell: its waves rolling in, growing, leaning and breaking over the shallowing
// floor, and its swash running up the sand and back (sea_waves.glsl, source/water/sea.hpp). Each level keeps only
// waves its vertices can show (as terrain.vert keeps the land's detail), so far water lies calm and levels still meet;
// the fragment shader shades the waves left out.

// The sea floor's shape is bound in the sea's own set (binding 1), where the water's set 1 holds the scene behind.
#define WOTAH_TERRAIN_SHAPE_SET     3
#define WOTAH_TERRAIN_SHAPE_BINDING 1

#include "noise.glsl"
#include "terrain_shape.glsl"
#include "clipmap.glsl"
#include "sea_waves.glsl"

layout(location = 0) in vec3 inGridPlace; // x, z: the vertex's place on the grid, in cells from the level's middle

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection; // world space to clip space: the camera and its perspective
    vec4 level;          // xy: the level's middle; z: its cells' size; w: half its width in cells
    vec4 surface;        // x: the still surface's height
} push;

layout(location = 0) out vec2  outRestPlace;     // where this water lies at rest (x, z), for the wind waves' normals
layout(location = 1) out float outShoal;         // the share of the wind waves that runs here (GetShoalShare)
layout(location = 2) out vec4  outSurf;          // the swell here: x its phase, y height, z peak, w lean (SurfPoint)
layout(location = 3) out float outViewDepth;     // how far in front of the camera, for the haze and the depth behind
layout(location = 4) out vec3  outWorldPosition; // where the point is in the world, for lighting and the view ray
layout(location = 5) out vec4  outSurfState;     // x: how broken the swell is; y: 1 where the swash's sheet (or bare
                                                 // sand) makes the surface, not the waves; z: the swash's foam; w: how
                                                 // far seaward of the swash's front (the water ends where it is 0)

// The shortest wave a level keeps, in its vertex spacings: as terrain.vert's DetailSpacings, so nothing shorter than
// four spacings flickers as the grid moves; the sand under the swash is taken at the very same detail as the land's.
const float WaveSpacings = 2.0;

// The narrowest swell front and crest the grid is given, in its vertex spacings: a breaking wave's front is far steeper
// than the grid's far levels can show, and running across their vertices it would shake; the fragment shader shades
// it as steep as it is.
const float FrontSpacings = 3.0;

void main()
{
    vec3  placed = PlaceClipmapVertex(inGridPlace.xz, push.level);
    vec2  rest   = placed.xy;
    float morph  = placed.z;

    // The spacing the vertex stands for: its level's, growing into the next coarser level's at the blended edge, so
    // both levels move the water at their edge the very same way.
    float spacing  = push.level.z * (1.0 + morph);
    float smallest = WaveSpacings * spacing;

    // The wind waves move the water to here.
    float shoal = GetShoalShare(rest);
    vec3  tilt;
    float lost;
    vec3  offset = GetWaveOffset(rest, shoal, smallest, tilt, lost);
    vec2  place  = rest + offset.xz;

    // The swell there, its full shape for the fragment shader; for the grid only as high, steep and sharp as it can
    // show its waves (they are shorter over shallow water); the sand under the swash, where it can reach.
    vec2      frame = GetSurfFrame(place);
    float     clock = seaWaves.swellClock.x;
    SurfPoint point = GetSurfPoint(frame.y, frame.x, clock);
    outSurf         = vec4(point.phase, point.height, point.peak, point.lean);
    float wavelength = GetSwellWavelength(point.depth);
    float narrowest  = FrontSpacings * spacing;
    point.height *= GetLayerShare(wavelength, smallest);
    point.lean = min(point.lean, max(1.0 - 2.0 * narrowest / wavelength, 0.0));
    point.peak = min(point.peak, max(1.0, pow(wavelength / (4.0 * narrowest), 2.0)));
    float sand  = HasSurf() && point.depth <= SwashDepth ? GetTerrainHeight(place, smallest) : 0.0;
    float sheet;
    float swashFoam;
    float behindFront;
    float height = GetSurfaceFrom(frame, point, clock, sand, offset.y, sheet, swashFoam, behindFront);

    outRestPlace     = rest;
    outShoal         = shoal;
    outSurfState     = vec4(point.broken, sheet, swashFoam, behindFront);
    outWorldPosition = vec3(place.x, height, place.y);
    gl_Position      = push.viewProjection * vec4(outWorldPosition, 1.0);

    // With a perspective projection, clip space's w is the distance along the camera's view direction.
    outViewDepth = gl_Position.w;
}
