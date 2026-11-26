#version 450
#extension GL_GOOGLE_include_directive : require

// The land's look (source/render/terrain_renderer.hpp): layers of ground, each where it belongs, blended softly: wet
// sand at the waterline and under the water, dry sand above it, grass in patches on higher, flatter ground (more of it
// the higher the ground: green spots on the beach, meadows on the hills), rock where it is steep. Each layer has its
// own color, varied by noise, and fine relief that catches the light up close (sand ripples, grass tufts, rough rock).
// Lit like everything else, fading into the haze far away.
//
// shortcut: the layers' looks are made here from noise, and their weights from rules. Textures (a texture array,
// sampled from three sides so slopes do not stretch them) would replace GetLayerLook, and painted weights (a paint
// mode) would adjust GetLayerWeights; both only ask where the point is, so nothing else changes.

#include "atmosphere.glsl"
#include "lighting.glsl"
#include "noise.glsl"
#include "terrain_shape.glsl"

layout(location = 0) in vec3  inWorldNormal;
layout(location = 3) in float inViewDepth;
layout(location = 4) in vec3  inWorldPosition;

layout(location = 0) out vec4 outColor;

// ---- The layers: x wet sand, y dry sand, z grass, w rock ----

// Their colors, by eye after photographs of a temperate sandy beach: pale warm sand, darker where wet, beach grass from
// pale straw to green, gray rock.
const vec3 DrySandColor   = vec3(0.80, 0.72, 0.56);
const vec3 WetSandColor   = vec3(0.50, 0.44, 0.33);
const vec3 DryGrassColor  = vec3(0.55, 0.54, 0.30);
const vec3 LushGrassColor = vec3(0.22, 0.40, 0.13);
const vec3 RockColor      = vec3(0.48, 0.46, 0.43);

// Where each layer belongs: heights in metres above the sea level, steepness as 1 minus the normal's y. By eye.
const float WetTop          = 0.5;  // sand is wet up to here (waves, spray), drying over WetFade
const float WetFade         = 0.35;
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
const float PatchEdge       = 0.05;

// How each layer varies, wavelengths in metres: its color (a share of it) and its fine relief (how far it tilts the
// normal). By eye, looking at the beach from a person's height.
const float SandMottleWavelength = 3.0;  // broad lighter and darker sand
const float SandMottle           = 0.05;
const float RippleWavelength     = 0.15; // wind ripples: crest to crest across them
const float RippleLength         = 0.9;  // and how far a crest runs along the coast before it breaks
const float RippleRelief         = 0.08;
const float GrassMixWavelength   = 9.0;  // straw to green
const float TuftWavelength       = 0.12; // tufts and blades
const float TuftShade            = 0.20;
const float TuftRelief           = 0.15;
const float RockMottleWavelength = 1.5;
const float RockMottle           = 0.15;
const float RockRelief           = 0.15;

// Each noise's own salt, so they differ from each other.
const uint PatchSalt  = 0x51ED270Bu;
const uint MottleSalt = 0x9E3779B1u;
const uint RippleSalt = 0x85EBCA77u;
const uint MixSalt    = 0xC2B2AE3Du;
const uint TuftSalt   = 0x27D4EB2Fu;
const uint RockSalt   = 0x165667B1u;

// How much of the layers there is at a point: rock where it is steep, grass on what rock leaves, sand on the rest,
// split into wet and dry by height; they add up to 1. footprint is the ground one pixel covers, in metres: detail
// finer than that is left out, so it does not flicker.
vec4 GetLayerWeights(vec3 position, vec3 normal, float footprint)
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

    float wet  = 1.0 - smoothstep(WetTop - WetFade, WetTop, above);
    float sand = (1.0 - rock) * (1.0 - grass);
    return vec4(sand * wet, sand * (1.0 - wet), (1.0 - rock) * grass, rock);
}

// A layer's look at a point: its color, and the tilt (along x and z) its fine relief gives the normal.
struct LayerLook
{
    vec3 color;
    vec2 tilt;
};

// Fine relief from a noise wavelength metres across: its slope, by relief, left out where a pixel covers it.
vec2 GetRelief(vec2 place, float wavelength, uint salt, float relief, float footprint)
{
    return relief * GetLayerShare(wavelength, 2.0 * footprint) * GetGradientNoise(place / wavelength, salt).yz;
}

// Sand: its color with broad mottling, and wind ripples: crests along the coast (in the coast's frame, stretched along
// it), so the relief runs mostly across them (its part along the coast scaled down by how much longer the crests are
// than their spacing), turned back into the world's x and z.
LayerLook GetSandLook(vec3 color, vec2 place, float footprint)
{
    float mottle =
        GetLayeredNoise(place / SandMottleWavelength, MottleSalt, 3, 2.0 * footprint / SandMottleWavelength);
    vec2 coast   = ToCoastFrame(place);
    vec3 ripple  = GetGradientNoise(vec2(coast.x / RippleLength, coast.y / RippleWavelength), RippleSalt);
    vec2 ripples = RippleRelief * GetLayerShare(RippleWavelength, 2.0 * footprint) *
                   FromCoastFrame(vec2(ripple.y * RippleWavelength / RippleLength, ripple.z));
    return LayerLook(color * (1.0 + SandMottle * mottle), ripples);
}

// Grass: straw to green in broad patches, darker between tufts, and the tufts' relief.
LayerLook GetGrassLook(vec2 place, float footprint)
{
    float green =
        0.5 + 0.5 * GetLayeredNoise(place / GrassMixWavelength, MixSalt, 2, 2.0 * footprint / GrassMixWavelength);
    float tufts = GetLayerShare(TuftWavelength, 2.0 * footprint) *
                  (0.5 + 0.5 * GetGradientNoise(place / TuftWavelength, TuftSalt).x);
    vec3 color  = mix(DryGrassColor, LushGrassColor, green) * (1.0 - TuftShade * tufts);
    return LayerLook(color, GetRelief(place, TuftWavelength, TuftSalt, TuftRelief, footprint));
}

// Rock: mottled gray, rough.
LayerLook GetRockLook(vec2 place, float footprint)
{
    float mottle =
        GetLayeredNoise(place / RockMottleWavelength, RockSalt, 2, 2.0 * footprint / RockMottleWavelength);
    vec2 rough = GetRelief(place, RockMottleWavelength, RockSalt, RockRelief, footprint);
    return LayerLook(RockColor * (1.0 + RockMottle * mottle), rough);
}

// The layers' looks blended by their weights; a layer with no weight is not looked at.
LayerLook GetLayerLook(vec4 weights, vec2 place, float footprint)
{
    LayerLook look = LayerLook(vec3(0.0), vec2(0.0));
    if (weights.x > 0.0)
    {
        LayerLook wet = GetSandLook(WetSandColor, place, footprint);
        look.color += weights.x * wet.color;
        look.tilt += weights.x * wet.tilt;
    }
    if (weights.y > 0.0)
    {
        LayerLook dry = GetSandLook(DrySandColor, place, footprint);
        look.color += weights.y * dry.color;
        look.tilt += weights.y * dry.tilt;
    }
    if (weights.z > 0.0)
    {
        LayerLook grass = GetGrassLook(place, footprint);
        look.color += weights.z * grass.color;
        look.tilt += weights.z * grass.tilt;
    }
    if (weights.w > 0.0)
    {
        LayerLook rock = GetRockLook(place, footprint);
        look.color += weights.w * rock.color;
        look.tilt += weights.w * rock.tilt;
    }
    return look;
}

void main()
{
    // The ground one pixel covers (measured here, where every pixel runs: derivatives are only defined outside
    // branches that differ from pixel to pixel).
    float footprint = max(length(fwidth(inWorldPosition.xz)), 1.0e-4);
    vec3  normal    = normalize(inWorldNormal);

    vec4      weights = GetLayerWeights(inWorldPosition, normal, footprint);
    LayerLook look    = GetLayerLook(weights, inWorldPosition.xz, footprint);

    // The relief tilts the normal against its slope, as a bump in the ground would.
    vec3 shading = normalize(normal - vec3(look.tilt.x, 0.0, look.tilt.y));
    vec3 lit     = Light(look.color, shading, inWorldPosition, inViewDepth);
    outColor     = vec4(ApplyHaze(lit, inViewDepth, sceneLights.hazeColor.rgb), 1.0);
}
