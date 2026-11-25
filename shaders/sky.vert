#version 450

// The sky: one triangle that covers the whole screen (no vertex buffer). Hands each pixel its screen position, from
// which sky.frag works out the direction it looks in.

layout(location = 0) out vec2 outScreenPosition; // -1 .. 1 across the screen, as in Vulkan's clip space

void main()
{
    // Vertices (-1, -1), (3, -1), (-1, 3): one triangle whose inside covers the square -1 .. 1.
    vec2 corner       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    outScreenPosition = corner;
    gl_Position       = vec4(corner, 0.0, 1.0);
}
