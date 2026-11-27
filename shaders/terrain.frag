#version 450
#extension GL_GOOGLE_include_directive : require

// The land's look (source/render/terrain_renderer.hpp): layers of ground, each where it belongs: wet sand at the
// waterline, sand above it and on the sea floor, grass in patches on higher, flatter ground (more of it the higher the
// ground: green spots on the beach, meadows on the hills), rock where it is steep. Each layer's look comes from its
// textures (source/render/terrain_materials.hpp: color, height, slope, roughness), tiles that repeat, laid out in
// hexagons each shifted (and turned, where the pattern allows) at random and blended where they meet, so no repeat
// shows (Mikkelsen 2022, "Practical Real-Time Hex-Tiling"). Where layers meet, whichever stands higher in its texture
// shows (sand settles between grass blades). Broad patches of color on top keep large areas from looking alike. Wet
// sand shines a little: where the sea's swash runs up and back (sea_waves.glsl), the sand it left is wet and dries over
// half a minute, and where the last run-ups stopped they left a line of foam; on a still sea a band at the waterline
// is wet. Lit like everything else, fading into the haze far away.
//
// shortcut: the layers' weights come from rules; painted weights (a paint mode) would adjust GetLayerWeights.

#include "atmosphere.glsl"
#include "lighting.glsl"
#include "noise.glsl"
#include "terrain_shape.glsl"
#include "ground.glsl"
#include "sea_waves.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

// The ground's textures (source/render/terrain_materials.hpp), a layer each (ground.glsl).
layout(set = 2, binding = 0) uniform sampler2DArray groundLook;    // rgb: color; a: height (0 to 1)
layout(set = 2, binding = 1) uniform sampler2DArray groundSurface; // rg: slope (ground.glsl); b: roughness

layout(location = 0) out vec4 outColor;

// ---- Where each layer belongs (x wet sand, y dry sand, z grass, w rock) ----

// Where each layer belongs: heights in metres above the sea level, steepness as 1 minus the normal's y. By eye.
const float WetTop          = 0.5;  // sand is wet up to here (waves, spray), drying over WetFade
const float WetFade         = 0.35;
const float SubmergedDry    = -0.2; // under the water, from here down, sand is seen through it, not wet on top
const float SubmergedFade   = 0.3;
const float GrassLine       = 1.4;  // grass starts to grow from here, fading in over GrassLineFade
const float GrassLineFade   = 0.4;
const float GrassFull       = 8.0;  // from here grass covers nearly all flat ground
const float GrassSteepStart = 0.30; // grass thins out on slopes from here and is gone at GrassSteepEnd
const float GrassSteepEnd   = 0.50;
const float RockSteepStart  = 0.45; // rock shows from here and covers all from RockSteepEnd
const float RockSteepEnd    = 0.70;

// Grass grows in patches: a patch noise PatchWavelength across must pass a threshold that falls from PatchSparse at the
// grass line to PatchDense high up; the patches' edges are PatchEdge wide in that noise. By eye.
const float PatchWavelength = 24.0;
const int   PatchLayers     = 5;
const float PatchSparse     = 0.62;
const float PatchDense      = 0.20;
const float PatchEdge       = 0.1;

// The patch noise's own salt.
const uint PatchSalt = 0x51ED270Bu;

// The swash's foam left on the sand: as the water's lace, but finer and sparser, by eye.
const vec3  TraceColor      = vec3(0.92, 0.94, 0.93);
const float TraceWavelength = 0.5;
const float TraceCover      = 0.75;
const uint  TraceSalt       = 0xBB67AE85u;

// How wet the sand is at position: the swash's (sea_waves.glsl) when the sea has surf, else a band at the waterline;
// never on top of sand seen through the water (it is seen through the water, not wet on top). Out: the swash's foam.
float GetWetness(vec3 position, out float trace)
{
    trace       = 0.0;
    float above = position.y - terrainShape.sea.x;
    float wet   = HasSurf() ? GetSandWetness(position.xz, trace) : 1.0 - smoothstep(WetTop - WetFade, WetTop, above);
    return wet * smoothstep(SubmergedDry - SubmergedFade, SubmergedDry, above);
}

// How much of the layers there is at a point: rock where it is steep, grass on what rock leaves, sand on the rest,
// split into wet (by wet, GetWetness) and dry; they add up to 1. footprint is the ground one pixel covers, in metres:
// detail finer than that is left out, so it does not flicker.
vec4 GetLayerWeights(vec3 position, vec3 normal, float footprint, float wet)
{
    float above = position.y - terrainShape.sea.x;
    float steep = 1.0 - normal.y;
    float rock  = smoothstep(RockSteepStart, RockSteepEnd, steep);

    float patches   = 0.5 + 0.5 * GetLayeredNoise(position.xz / PatchWavelength, PatchSalt, PatchLayers,
                                                  2.0 * footprint / PatchWavelength);
    float threshold = mix(PatchSparse, PatchDense, smoothstep(GrassLine, GrassFull, above));
    float grass     = smoothstep(threshold, threshold + PatchEdge, patches) *
                  smoothstep(GrassLine - GrassLineFade, GrassLine + GrassLineFade, above) *
                  (1.0 - smoothstep(GrassSteepStart, GrassSteepEnd, steep));

    float sand = (1.0 - rock) * (1.0 - grass);
    return vec4(sand * wet, sand * (1.0 - wet), (1.0 - rock) * grass, rock);
}

// ---- How the layers look ----

// The hexagons the tiles are laid out in: HexesPerTile across a tile, each tile's corner weights sharpened by
// HexSharpness so the hexagons' middles show one tile clearly and only their edges blend (Mikkelsen 2022). By eye.
const float HexesPerTile = 0.6;
const float HexSharpness = 4.0;
const uint  HexSalt      = 0xA54FF53Au;

// Where two layers meet, each stands as high as HeightBlend times its texture's height above its weight (sand's low
// ripples count little, so grass rises out of it in tufts); within HeightWindow of the highest, layers blend, the rest
// are hidden. By eye.
const vec4  HeightBlend  = vec4(0.1, 0.1, 0.6, 0.4);
const float HeightWindow = 0.12;

// How strongly each layer's texture slopes tilt the light (1: as the slopes are). By eye.
const float Relief[GroundLayerCount] = float[](0.6, 0.6, 0.6, 0.8);

// Broad patches over the textures, so large stretches do not look alike: sand lighter and darker by SandMottle over
// SandMottleWavelength metres (and half as much over four times that); grass from lush to straw over
// GrassMixWavelength metres (its texture's colors tinted by LushTint and StrawTint); rock by RockMottle. By eye.
const float SandMottleWavelength = 3.0;
const float SandMottle           = 0.06;
const float GrassMixWavelength   = 9.0;
const vec3  LushTint             = vec3(0.85, 0.98, 0.85);
const vec3  StrawTint            = vec3(1.75, 1.3, 1.6);
const float RockMottleWavelength = 6.0;
const float RockMottle           = 0.1;
const uint  MottleSalt           = 0x9E3779B1u;
const uint  MixSalt              = 0xC2B2AE3Du;
const uint  RockSalt             = 0x165667B1u;

// Wet sand's film of water throws back about 2 % of the light straight on (water's Fresnel), more at a slant; it is
// never smoother than WetSmoothest (the grains poke through it), so the sheen does not break into sparkling dots.
const float WetReflectance = 0.02;
const float WetSmoothest   = 0.3;

// A layer's texture at a point: its color, height (0 to 1), slope (along x and z, metres per metre) and roughness.
struct Ground
{
    vec3  color;
    float height;
    vec2  slope;
    float roughness;
};

// The three hexagons around uv (Mikkelsen 2022, after Heitz and Neyret 2018): their corners on a skewed triangle grid
// (out) and how much each counts, adding up to 1.
vec3 GetHexes(vec2 uv, out ivec2 first, out ivec2 second, out ivec2 third)
{
    vec2  scaled = uv * 3.46410162; // 2 sqrt(3)
    vec2  skewed = vec2(scaled.x - 0.57735027 * scaled.y, 1.15470054 * scaled.y);
    ivec2 base   = ivec2(floor(skewed));
    vec3  inside = vec3(fract(skewed), 0.0);
    inside.z     = 1.0 - inside.x - inside.y;
    float upper  = step(0.0, -inside.z);
    float flip   = 2.0 * upper - 1.0;
    first        = base + ivec2(upper, upper);
    second       = base + ivec2(upper, 1.0 - upper);
    third        = base + ivec2(1.0 - upper, upper);
    return vec3(-inside.z * flip, upper - inside.y * flip, upper - inside.x * flip);
}

// One hexagon's tile of a layer at uv: shifted at random (by whole repeats across, where the layer asks), turned at
// random where it may be; its slope turned back into the world's directions. dx and dy are how uv changes from one
// pixel to the next, so the texture's level of detail fits.
Ground SampleTile(int layer, vec2 uv, vec2 dx, vec2 dy, ivec2 hex)
{
    vec3  random = GetCellRandom(vec3(vec2(hex), float(layer)), HexSalt);
    float angle  = GroundTurns[layer] ? 6.28318531 * random.z : 0.0;
    mat2  turn   = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    vec2  shift  = random.xy * 8.0;
    if (GroundRepeatsAcross[layer] > 0.0)
    {
        shift.x = floor(shift.x * GroundRepeatsAcross[layer]) / GroundRepeatsAcross[layer];
    }
    vec3 at      = vec3(turn * uv + shift, float(layer));
    vec4 look    = textureGrad(groundLook, at, turn * dx, turn * dy);
    vec4 surface = textureGrad(groundSurface, at, turn * dx, turn * dy);
    vec2 slope   = transpose(turn) * ((surface.xy - 0.5) / SlopeEncoding);
    return Ground(look.rgb, look.a, slope, surface.b);
}

// A layer's ground at a place (metres), laid out in hexagons; placeDx and placeDy are how the place changes from one
// pixel to the next.
Ground SampleLayer(int layer, vec2 place, vec2 placeDx, vec2 placeDy)
{
    float tile = GroundTileSize[layer];
    vec2  uv   = place / tile;
    vec2  dx   = placeDx / tile;
    vec2  dy   = placeDy / tile;
    ivec2 hexes[3];
    vec3  corners = GetHexes(uv * HexesPerTile, hexes[0], hexes[1], hexes[2]);
    vec3  weights = pow(corners, vec3(HexSharpness));
    weights /= dot(weights, vec3(1.0));

    Ground sum = Ground(vec3(0.0), 0.0, vec2(0.0), 0.0);
    for (int index = 0; index < 3; ++index)
    {
        Ground tileGround = SampleTile(layer, uv, dx, dy, hexes[index]);
        sum.color += weights[index] * tileGround.color;
        sum.height += weights[index] * tileGround.height;
        sum.slope += weights[index] * tileGround.slope;
        sum.roughness += weights[index] * tileGround.roughness;
    }
    return sum;
}

// The layers' weights reshaped by their heights: each stands HeightBlend times its height above its weight, and only
// those within HeightWindow of the highest show, blended; so grass rises out of sand in tufts, not as a smooth fade.
vec4 BlendByHeight(vec4 weights, vec4 heights)
{
    vec4  present = step(1.0e-4, weights);
    vec4  raised  = (weights + HeightBlend * heights) * present;
    float top     = max(max(raised.x, raised.y), max(raised.z, raised.w));
    vec4  kept    = max(raised - (top - HeightWindow), 0.0) * present;
    return kept / max(dot(kept, vec4(1.0)), 1.0e-6);
}

// The broad patches' tint for each layer at a place, so large stretches differ: sand lighter or darker, grass from
// lush to straw, rock lighter or darker. footprint leaves out what a pixel covers.
void GetTints(vec2 place, float footprint, out vec3 sand, out vec3 grass, out vec3 rock)
{
    float smallest = 2.0 * footprint / SandMottleWavelength;
    float mottle   = GetLayeredNoise(place / SandMottleWavelength, MottleSalt, 3, smallest) +
                   0.5 * GetLayeredNoise(place / (4.0 * SandMottleWavelength), MottleSalt + 1u, 2, 0.0);
    sand          = vec3(1.0 + SandMottle * mottle);
    float dryness = smoothstep(-0.25, 0.25, GetLayeredNoise(place / GrassMixWavelength, MixSalt, 2,
                                                             2.0 * footprint / GrassMixWavelength));
    grass         = mix(LushTint, StrawTint, dryness);
    rock          = vec3(1.0 + RockMottle * GetLayeredNoise(place / RockMottleWavelength, RockSalt, 2, 0.0));
}

void main()
{
    // The ground one pixel covers, and how the place changes from pixel to pixel (measured here, where every pixel
    // runs: derivatives are only defined outside branches that differ from pixel to pixel).
    vec2  place     = inWorldPosition.xz;
    vec2  placeDx   = dFdx(place);
    vec2  placeDy   = dFdy(place);
    float footprint = max(length(fwidth(place)), 1.0e-4);
    vec3  normal    = normalize(inWorldNormal);

    // Every layer that is there, from its textures, blended by weight and height; each tinted by the broad patches.
    float  trace;
    float  wet     = GetWetness(inWorldPosition, trace);
    vec4   weights = GetLayerWeights(inWorldPosition, normal, footprint, wet);
    Ground layers[GroundLayerCount];
    vec4   heights = vec4(0.0);
    for (int layer = 0; layer < GroundLayerCount; ++layer)
    {
        layers[layer] = Ground(vec3(0.0), 0.0, vec2(0.0), 1.0);
        if (weights[layer] > 0.0)
        {
            layers[layer]  = SampleLayer(layer, place, placeDx, placeDy);
            heights[layer] = layers[layer].height;
        }
    }
    vec4 blend = BlendByHeight(weights, heights);
    vec3 sandTint;
    vec3 grassTint;
    vec3 rockTint;
    GetTints(place, footprint, sandTint, grassTint, rockTint);
    vec3 tints[GroundLayerCount] = vec3[](sandTint, sandTint, grassTint, rockTint);

    vec3  color     = vec3(0.0);
    vec2  tilt      = vec2(0.0);
    float roughness = 0.0;
    for (int layer = 0; layer < GroundLayerCount; ++layer)
    {
        color += blend[layer] * layers[layer].color * tints[layer];
        tilt += blend[layer] * Relief[layer] * layers[layer].slope;
        roughness += blend[layer] * layers[layer].roughness;
    }

    // The swash's foam on the sand: thin bubbly threads, a faint even share where they are finer than the pixel.
    float threads = pow(1.0 - min(abs(GetLayeredNoise(place / TraceWavelength, TraceSalt, 3,
                                                      2.0 * footprint / TraceWavelength)) * 2.0, 1.0), 4.0);
    float lace    = mix(0.3, threads, GetLayerShare(TraceWavelength, 2.0 * footprint));
    color         = mix(color, TraceColor, TraceCover * trace * lace * blend.x);

    // The relief tilts the normal against its slope, as a bump in the ground would; wet sand's film shines.
    vec3 shading       = normalize(normal - vec3(tilt.x, 0.0, tilt.y));
    vec3 towardsCamera = normalize(sceneLights.cameraPosition.xyz - inWorldPosition);
    vec3 lit           = Light(color, shading, inWorldPosition, inViewDepth);
    lit += blend.x * LightGloss(shading, towardsCamera, inWorldPosition, inViewDepth, max(roughness, WetSmoothest),
                                WetReflectance);
    outColor = vec4(ApplyHaze(lit, inViewDepth, sceneLights.hazeColor.rgb), 1.0);
}
