// The sea's waves (#include "sea_waves.glsl", after noise.glsl; after terrain_shape.glsl for the parts that feel the
// floor): the wind's Gerstner waves and the swell's surf of source/water/sea.hpp, as source/render/sea_state.cpp writes
// them into the uniform buffer at set 3. It mirrors source/water/sea.cpp line by line; a change to one must go into the
// other (tests/gpu_tests.cpp compares them).

#ifndef WOTAH_SEA_WAVES_GLSL
#define WOTAH_SEA_WAVES_GLSL

// A whole turn and a half one, in radians (source/water/sea.cpp's TwoPi and pi, the same floats), and gravity (physics'
// StandardGravity).
const float SeaTwoPi   = 6.28318531;
const float SeaPi      = 3.14159265;
const float SeaGravity = 9.80665;

// Matches GpuSea in source/render/sea_state.cpp (std140: vec4s only).
layout(std140, set = 3, binding = 0) uniform SeaWavesBlock
{
    uvec4 count;      // x: how many wind waves run; y: 1 when the sea has a floor (terrain_shape.glsl)
    vec4  shoal;      // x: GetShoalRate; z: WaveFloorDetail; w: the still surface's height
    vec4  swell;      // xy: its direction; z: its height (0: none); w: its period
    vec4  swellClock; // x: GetSwellClock now
    vec4  waves[2 * WOTAH_MAX_SEA_WAVES]; // per wave: [2i] xy direction, z wavelength, w amplitude;
                                          // [2i + 1] x steepness, y phase now (GetWavePhase), z wavenumber
} seaWaves;

// ---- The wind's waves ----

// The water that lies at rest at restPlace (x, z) with shoal its share of the waves (GetShoalShare), waves shorter than
// smallest metres left out (those up to twice as long faded, noise.glsl's GetLayerShare): where it is now, as an
// offset from there. Out: the surface's tilt there (GetWaveNormal turns it into the normal), and the mean square slope
// of the waves left out, which the surface's roughness takes instead.
vec3 GetWaveOffset(vec2 restPlace, float shoal, float smallest, out vec3 tilt, out float lostSpread)
{
    vec3 offset = vec3(0.0);
    tilt        = vec3(0.0);
    lostSpread  = 0.0;
    uint used   = min(seaWaves.count.x, uint(WOTAH_MAX_SEA_WAVES));
    for (uint index = 0u; index < used; ++index)
    {
        vec4  form      = seaWaves.waves[2u * index];
        vec4  motion    = seaWaves.waves[2u * index + 1u];
        vec2  direction = form.xy;
        float shown     = GetLayerShare(form.z, smallest);
        float amplitude = shoal * form.w * shown;
        float steepness = motion.x;
        float number    = motion.z;

        // Whole wavelengths dropped first, so the sine's angle stays small and exact far from the origin.
        float cycles = dot(direction, restPlace) / form.z;
        float angle  = SeaTwoPi * (cycles - floor(cycles)) + motion.y;
        float along  = cos(angle);
        float across = sin(angle);

        // Up and down in a sine, back and forth along the wave in step: crests gather the water, troughs spread it.
        offset += vec3(direction.x * steepness * amplitude * along, amplitude * across,
                       direction.y * steepness * amplitude * along);

        // The surface's tilt (GPU Gems 1, chapter 1, equation 12).
        float slope = number * amplitude;
        tilt += vec3(direction.x * slope * along, steepness * slope * across, direction.y * slope * along);

        // A sine's slope squared averages half its peak's square.
        float lostSlope = number * shoal * form.w * (1.0 - shown);
        lostSpread += 0.5 * lostSlope * lostSlope;
    }
    return offset;
}

// The surface's normal from GetWaveOffset's tilt.
vec3 GetWaveNormal(vec3 tilt)
{
    return normalize(vec3(-tilt.x, 1.0 - tilt.y, -tilt.z));
}

// ---- The swell's waveform ----

// Where the swell stands at a place (source/water/sea.hpp, SurfPoint).
struct SurfPoint
{
    float phase;
    float height;
    float peak;
    float lean;
    float broken;
    float setup;
    float depth;
};

// The swell's height over the mean water at point (its waveform), and (out) how fast it changes with the phase.
float GetSurfWave(SurfPoint point, out float rise)
{
    // Within the wave: 0 at a crest, rising seaward over its back to the trough, then up its front to the next crest.
    // The front takes less of the wave the more it leans; both halves are half cosines.
    float turns = point.phase / SeaTwoPi;
    float place = turns - floor(turns);
    float front = 0.5 * (1.0 - point.lean);
    float back  = 1.0 - front;
    float lift;
    float liftRise;
    if (place < back)
    {
        lift     = 0.5 * (1.0 + cos(SeaPi * place / back));
        liftRise = -0.5 * SeaPi / back * sin(SeaPi * place / back);
    }
    else
    {
        lift     = 0.5 * (1.0 - cos(SeaPi * (place - back) / front));
        liftRise = 0.5 * SeaPi / front * sin(SeaPi * (place - back) / front);
    }

    // Sharper crests and flatter troughs, kept around the mean water: lift^p averages about 1 / sqrt(pi (p + 1/4)).
    float mean = 1.0 / sqrt(SeaPi * (point.peak + 0.25));
    rise       = point.height * point.peak * pow(max(lift, 1.0e-6), point.peak - 1.0) * liftRise / SeaTwoPi;
    return point.height * (pow(lift, point.peak) - mean);
}

#ifdef WOTAH_TERRAIN_SHAPE_GLSL
// ---- What feels the floor (source/water/sea.cpp) ----

const float SwellSetWaves    = 7.0;
const float SetDepth         = 0.3;
const float AlongVariation   = 0.2;
const float AlongWavelength  = 90.0;
const uint  AlongSalt        = 0x5BD1E995u;
const float MaxAlongShare    = 0.8;
const float NoFloorDepth     = 1.0e4;
const float ShallowestDepth  = 0.02;
const float MaxShoaling      = 2.5;
const float BreakingShare    = 0.78;
const float BreakStart       = 0.9;
const float BreakEnd         = 1.3;
const float BrokenDepthShare = 0.5;
const float PeakStart        = 0.35;
const float PeakGrowth       = 1.2;
const float LeanStart        = 0.6;
const float MaxLean          = 0.85;
const float BoreLean         = 0.9;
const float SetupShare       = 0.15;
const float RunUpFactor      = 1.1;
const float SetupFactor      = 0.35;
const float SlopeFactor      = 0.563;
const float SwashFloor       = 0.004;
const float SwashSlowing     = 1.2;
const float SwashShare       = 0.85;
const float DrawShare        = 0.5;
const float SheetThinnest    = 0.02;
const float SheetShare       = 0.3;
const float SheetRamp        = 3.0;
const float SheetRise        = 0.6;
const float SheetBlend       = 0.03; // the shaders only: where sheet and waves meet, metres over which they blend
const float SandCover        = 0.05;
const float FrontMargin      = 2.0;
const float WetEdge          = 0.5;
const float SwashDepth       = 3.0;
const float DryingSeconds    = 25.0;
const float TraceSeconds     = 15.0;
const float TraceWidth       = 0.25;
const int   WetWaves         = 6;

const float QuadratureNodes[8]   = float[](0.01985507, 0.10166676, 0.23723380, 0.40828268, 0.59171732, 0.76276620,
                                           0.89833324, 0.98014493);
const float QuadratureWeights[8] = float[](0.05061427, 0.11119052, 0.15685332, 0.18134189, 0.18134189, 0.15685332,
                                           0.11119052, 0.05061427);

// The share of the wind's waves that runs at place (x, z): 1 over deep water, none at the shore and on land, as the
// floor's depth lets them stand.
float GetShoalShare(vec2 place)
{
    if (seaWaves.count.y == 0u || !(seaWaves.shoal.x > 0.0))
    {
        return 1.0;
    }
    float depth = seaWaves.shoal.w - GetTerrainHeight(place, seaWaves.shoal.z);
    return smoothstep(0.0, 1.0, depth * seaWaves.shoal.x);
}

bool HasFloor()
{
    return seaWaves.count.y != 0u;
}

bool HasSurf()
{
    return HasFloor() && seaWaves.swell.z > 0.0 && seaWaves.swell.x > 0.0;
}

float GetSwellFrequency()
{
    return SeaTwoPi / seaWaves.swell.w;
}

float GetDeepNumber()
{
    float frequency = GetSwellFrequency();
    return frequency * frequency / SeaGravity;
}

float GetAlongNumber(float deep)
{
    return HasFloor() ? deep * clamp(seaWaves.swell.y, -MaxAlongShare, MaxAlongShare) : 0.0;
}

float GetLocalNumber(float deep, float depth)
{
    return deep / sqrt(tanh(deep * max(depth, ShallowestDepth)));
}

// The swell's wavelength over water depth deep, in metres: shorter over shallow water.
float GetSwellWavelength(float depth)
{
    return SeaTwoPi / GetLocalNumber(GetDeepNumber(), depth);
}

float GetShoaling(float deep, float local, float depth, float frequency)
{
    float twice = 2.0 * local * max(depth, ShallowestDepth);
    float share = 0.5 * (1.0 + (twice < 20.0 ? twice / sinh(twice) : 0.0));
    float group = frequency / local * share;
    float far   = frequency / (2.0 * deep);
    return min(sqrt(far / group), MaxShoaling);
}

float GetSwellDepth(float seaward)
{
    return HasFloor() ? GetBeachDepth(seaward) : NoFloorDepth;
}

float GetPhaseAcross(float seaward, float deep, float alongNumber)
{
    if (!HasFloor())
    {
        return deep * seaward;
    }
    if (!(seaward > 0.0))
    {
        return 0.0;
    }
    float sum = 0.0;
    for (int index = 0; index < 8; ++index)
    {
        float node   = QuadratureNodes[index];
        float local  = GetLocalNumber(deep, GetBeachDepth(seaward * node * node));
        float across = sqrt(max(local * local - alongNumber * alongNumber, 0.04 * local * local));
        sum += QuadratureWeights[index] * across * 2.0 * seaward * node;
    }
    return sum;
}

float GetSetShare(float phase, float along)
{
    float peaks = 1.0 + AlongVariation * GetGradientNoise(vec2(along / AlongWavelength, 0.0), AlongSalt).x;
    return (1.0 + SetDepth * sin(phase / SwellSetWaves)) * peaks;
}

float GetLastCrest(float phase)
{
    return phase - SeaTwoPi * (phase / SeaTwoPi - floor(phase / SeaTwoPi));
}

float GetRunUp(float height, float period, float slope)
{
    float deepLength = SeaGravity * period * period / SeaTwoPi;
    float root       = sqrt(height * deepLength);
    return RunUpFactor * (SetupFactor * slope * root + 0.5 * root * sqrt(SlopeFactor * slope * slope + SwashFloor));
}

// A place (x, z) in the swell's frame: x along the coast, y seaward of the waterline.
vec2 GetSurfFrame(vec2 place)
{
    if (HasFloor())
    {
        vec3 coast = GetCoastPlace(place, seaWaves.shoal.z);
        return vec2(coast.x, coast.z);
    }
    vec2 way = seaWaves.swell.xy;
    return vec2(way.x * place.y - way.y * place.x, -dot(way, place));
}

// The swell at a place in its frame, at the swell clock clock.
SurfPoint GetSurfPoint(float seaward, float along, float clock)
{
    SurfPoint point = SurfPoint(0.0, 0.0, 1.0, 0.0, 0.0, 0.0, GetSwellDepth(seaward));
    if (!(seaWaves.swell.z > 0.0) || (HasFloor() && !(seaWaves.swell.x > 0.0)))
    {
        return point;
    }
    float frequency   = GetSwellFrequency();
    float deep        = GetDeepNumber();
    float alongNumber = GetAlongNumber(deep);
    point.phase       = GetPhaseAcross(seaward, deep, alongNumber) - alongNumber * along + clock;
    float set         = GetSetShare(point.phase, along);
    if (!(point.depth > 0.0))
    {
        point.broken = 1.0;
        point.setup  = SetupShare * seaWaves.swell.z * set;
        return point;
    }
    float local          = GetLocalNumber(deep, point.depth);
    float unbroken       = seaWaves.swell.z * set * GetShoaling(deep, local, point.depth, frequency);
    float breaking       = BreakingShare * point.depth;
    float ratio          = unbroken / breaking;
    point.broken         = smoothstep(BreakStart, BreakEnd, ratio);
    point.height         = mix(unbroken, BrokenDepthShare * point.depth, point.broken);
    float unbrokenShare  = 1.0 - point.broken;
    point.peak           = 1.0 + PeakGrowth * smoothstep(PeakStart, 1.0, ratio) * unbrokenShare;
    point.lean           = MaxLean * smoothstep(LeanStart, 1.0, ratio) * unbrokenShare + BoreLean * point.broken;
    point.setup          = SetupShare * seaWaves.swell.z * set * point.broken;
    return point;
}

// One wave's swash (source/water/sea.cpp, SwashCycle): x the mean waterline, y how far it runs up, z how far it draws
// back, w seconds from its start to its highest point; since: seconds since it started; runUp: its run-up.
// A swash's size from its wave's height (GetSwashSize): x the mean waterline, y how far up, z how far back, w when it
// peaks; out: its run-up.
vec4 GetSwashSize(float height, float period, float slope, out float runUp)
{
    runUp       = GetRunUp(height, period, slope);
    float shore = -SetupShare * height / slope;
    float up    = runUp / slope;
    float down  = DrawShare * up;
    float most  = 0.5 * SwashSlowing * pow(0.5 * SwashShare * period, 2.0);
    float fit   = min(1.0, most / (up + down));
    up *= fit;
    down *= fit;
    return vec4(shore, up, down, sqrt(2.0 * (up + down) / SwashSlowing));
}

vec4 GetSwashCycle(float along, float clock, int waves, out float since, out float runUp)
{
    float slope       = terrainShape.sea.y;
    float deep        = GetDeepNumber();
    float alongNumber = GetAlongNumber(deep);
    float usualRunUp;
    vec4  usual   = GetSwashSize(seaWaves.swell.z, seaWaves.swell.w, slope, usualRunUp);
    float drawn   = max(usual.x + usual.z, 0.0);
    float atDrawn = -alongNumber * along + clock + GetPhaseAcross(drawn, deep, alongNumber);
    float turns   = atDrawn / SeaTwoPi - floor(atDrawn / SeaTwoPi);
    float crest   = GetLastCrest(atDrawn) - SeaTwoPi * float(waves);
    vec4  own     = GetSwashSize(seaWaves.swell.z * GetSetShare(crest, along), seaWaves.swell.w, slope, runUp);
    float most    = 0.5 * SwashSlowing * pow(0.5 * SwashShare * seaWaves.swell.w, 2.0);
    vec4  cycle   = usual;
    cycle.y       = min(own.y, most - usual.z);
    cycle.w       = sqrt(2.0 * (cycle.y + cycle.z) / SwashSlowing);
    since         = (turns + float(waves)) * seaWaves.swell.w;
    return cycle;
}

float GetSwashReach(vec4 cycle, float seconds)
{
    if (seconds >= 2.0 * cycle.w)
    {
        return -cycle.z;
    }
    float early = seconds - cycle.w;
    return cycle.y - 0.5 * SwashSlowing * early * early;
}

// The swash at along, clock: x where its front is (metres seaward), y the sheet's thinnest depth, z how much deeper;
// w how fresh it is: 1 as its bore arrives, 0 once it has run back (the shaders' foam; not in source/water/sea.cpp).
vec4 GetSwash(float along, float clock)
{
    if (!HasSurf())
    {
        return vec4(-NoFloorDepth, 0.0, 0.0, 0.0);
    }
    float since;
    float runUp;
    vec4  cycle = GetSwashCycle(along, clock, 0, since, runUp);
    float fresh = min(since / SheetRise, 1.0) * max(0.0, 1.0 - since / (2.0 * cycle.w));
    return vec4(cycle.x - GetSwashReach(cycle, since), SheetThinnest, SheetShare * runUp * fresh, fresh);
}

// The swash's whitewater: a fresh run-up foams most within SwashFoamLength metres behind its front, less over the rest
// of its sheet (SwashFoamBase of it), its foam fading as it runs back. By eye after photographs of swash.
const float SwashFoamLength = 3.0;
const float SwashFoamBase   = 0.35;

// The water surface's height from the swell's frame and point there, over sand, with the wind waves' lift: the still
// level, the swell's surf and swash (source/water/sea.cpp, GetSurfaceHeight). Out: 1 where the swash decides it (its
// sheet, or bare sand), else 0; how much the swash foams there; and how far seaward of its front the place lies (the
// water is cut off where that is below 0; very far without a swash).
float GetSurfaceFrom(vec2 frame, SurfPoint point, float clock, float sand, float windLift, out float sheet,
                     out float swashFoam, out float behindFront)
{
    sheet         = 0.0;
    swashFoam     = 0.0;
    behindFront   = NoFloorDepth;
    float rise;
    float surface = seaWaves.shoal.w + windLift;
    if (!(seaWaves.swell.z > 0.0))
    {
        return surface;
    }
    surface += point.setup + GetSurfWave(point, rise);
    if (!HasSurf() || point.depth > SwashDepth)
    {
        return surface;
    }
    vec4 swash  = GetSwash(frame.x, clock);
    behindFront = frame.y - swash.x;
    if (frame.y < swash.x)
    {
        sheet = 1.0;
        return swash.x - frame.y < FrontMargin ? sand + swash.y : min(surface, sand - SandCover);
    }
    float behind = clamp((frame.y - swash.x) / SheetRamp, 0.0, 1.0);
    float cover  = sand + swash.y + swash.z * sqrt(behind);
    sheet        = smoothstep(-SheetBlend, SheetBlend, cover - surface);
    swashFoam    = swash.w * max(exp(-(frame.y - swash.x) / SwashFoamLength), SwashFoamBase * sheet);
    return max(surface, cover);
}

// How wet the sand is at place (x, z) of the beach, and (out) the foam the swash left there (GetSandWetness).
float GetSandWetness(vec2 place, out float trace)
{
    trace = 0.0;
    if (!HasSurf())
    {
        return 0.0;
    }
    vec2  frame   = GetSurfFrame(place);
    float clock   = seaWaves.swellClock.x;
    float wetness = 0.0;
    for (int waves = 0; waves < WetWaves; ++waves)
    {
        float since;
        float runUp;
        vec4  cycle = GetSwashCycle(frame.x, clock, waves, since, runUp);
        float up    = cycle.x - frame.y;
        if (up <= -cycle.z)
        {
            return 1.0;
        }
        if (up > cycle.y + WetEdge)
        {
            continue;
        }
        float aside = sqrt(2.0 * max(cycle.y - up, 0.0) / SwashSlowing);
        if (since < cycle.w - aside)
        {
            continue;
        }
        if (up <= cycle.y && since <= cycle.w + aside)
        {
            return 1.0;
        }
        float counted = clamp(float(WetWaves) - since / seaWaves.swell.w, 0.0, 1.0);
        float soaked  = 1.0 - smoothstep(cycle.y, cycle.y + WetEdge, up);
        wetness       = max(wetness, counted * soaked * exp(-(since - cycle.w - aside) / DryingSeconds));
        float stopped = since - cycle.w;
        trace = max(trace, counted * exp(-stopped / TraceSeconds) *
                               (1.0 - smoothstep(0.0, TraceWidth, abs(up - cycle.y))));
    }
    return wetness;
}
#endif

#endif // WOTAH_SEA_WAVES_GLSL
