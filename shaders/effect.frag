#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 local;

layout(location = 0) out vec4 color;

const float RING_RADIUS = 0.64;
const float RING_FADE_START = 0.08;
const float RING_FADE_END = 0.28;

const float OUTER_FADE_START = 0.91;
const float OUTER_RADIUS = 1.0;

void main() {
    float radius = length(local);

    float distanceFromRing = abs(radius - RING_RADIUS);

    float ringBand =
        1.0 - smoothstep(RING_FADE_START, RING_FADE_END, distanceFromRing);

    float outerFade =
        1.0 - smoothstep(OUTER_FADE_START, OUTER_RADIUS, radius);

    float alpha = ringBand * outerFade;

    color = vec4(vertexColor.rgb, vertexColor.a * alpha);
}