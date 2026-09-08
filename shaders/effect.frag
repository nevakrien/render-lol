#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec2 local;
layout(location = 2) in flat float shapeId;
layout(location = 3) in vec3 effectData;

layout(location = 0) out vec4 color;

const float RING_RADIUS = 0.64;
const float RING_FADE_START = 0.08;
const float RING_FADE_END = 0.28;

const float OUTER_FADE_START = RING_FADE_END;
const float OUTER_FADE_END = 0.84;

const float RECT_HALF_X = 0.85;
const float RECT_HALF_Y = 0.65;
const float FULL_INTENSITY_STRENGTH = 0.35;

float sdEquilateralTriangle(vec2 p) {
    const float k = sqrt(3.0);
    p.x = abs(p.x) - RING_RADIUS;
    p.y = p.y + RING_RADIUS / k;
    if (p.x + k * p.y > 0.0)
        p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * RING_RADIUS, 0.0);
    return -length(p) * sign(p.y);
}

void main() {
    float d;
    if (shapeId > 0.5 && shapeId < 1.5) {
        d = sdEquilateralTriangle(local);
    } else if (shapeId > 1.5 && shapeId < 2.5) {
        vec2 b = vec2(RECT_HALF_X, RECT_HALF_Y);
        vec2 q = abs(local) - b;
        d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
    } else {
        d = length(local) - RING_RADIUS;
    }

    float dist = abs(d);
    float ringBand = 1.0 - smoothstep(RING_FADE_START, RING_FADE_END, dist);
    float outerFade = 1.0 - smoothstep(OUTER_FADE_START, OUTER_FADE_END, d);

    float intensity = smoothstep(0.0, FULL_INTENSITY_STRENGTH, effectData.z);
    float alpha = ringBand * outerFade * intensity;

    color = vec4(vertexColor.rgb, vertexColor.a * alpha);
}
