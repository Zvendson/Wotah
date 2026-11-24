#version 450

// Orange, lit by one fixed light from above, so every face of the cube has its own brightness; each face has a black
// border, so its edges show from any side.

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inCubePosition; // -0.5 .. 0.5 per axis

layout(location = 0) out vec4 outColor;

const vec3  BaseColor      = vec3(1.0, 0.5, 0.0);           // orange
const vec3  LightDirection = normalize(vec3(0.4, 1.0, 0.25)); // towards the light: up, a bit right and front
const float Ambient        = 0.25;                           // light that reaches faces turned away
const vec3  EdgeColor      = vec3(0.0);                      // black
const float EdgeWidth      = 1.5;  // in screen pixels per face; where two faces meet, their borders add up
const float EdgeSoftness   = 1.0;  // pixels over which the border fades into the face, so it does not look jagged
const float HalfSize       = 0.5;  // the cube reaches from -0.5 to 0.5 on every axis
const float NotAnEdge      = 1.0e6; // pushes the face's own axis out of the edge search

void main()
{
    float diffuse = max(dot(normalize(inNormal), LightDirection), 0.0);
    vec3  lit     = BaseColor * (Ambient + (1.0 - Ambient) * diffuse);

    // The distance to the face's border along each axis, in cube units. On a face, the axis it faces is always at the
    // border (|position| = 0.5), so that axis is left out; the nearest of the other two is the nearest edge.
    vec3 toBorder = HalfSize - abs(inCubePosition) + abs(inNormal) * NotAnEdge;

    // fwidth: how many cube units one screen pixel covers here, so the border keeps its width in pixels at any
    // distance and angle.
    vec3  toBorderInPixels = toBorder / max(fwidth(toBorder), vec3(1.0e-6));
    float nearestEdge      = min(toBorderInPixels.x, min(toBorderInPixels.y, toBorderInPixels.z));
    float edge             = 1.0 - smoothstep(EdgeWidth - EdgeSoftness, EdgeWidth, nearestEdge);

    outColor = vec4(mix(lit, EdgeColor, edge), 1.0);
}
