#version 450

layout(location=1) in vec2 local;
layout(location=3) in vec3 effectData;
layout(location=0) out vec4 outColor;

const float PI = 3.14159265359;

float hashNoise(vec2 p, float t, float seed) {
    p += t * 0.1;
    float freq1 = sin(2.0 * p.x) * sin(3.0 * p.y);
    float freq2 = sin(5.0 * p.x + t) * cos(5.0 * p.y - t);
    float freq4 = cos(7.0 * seed * p.x) * sin(11.0 * seed * p.y);
    float combined = 0.1 * freq1 + freq2 + freq4;
    return clamp(combined * combined, 0.0, 1.0);
}

void main() {
    float t = effectData.x / 0.2;
    if (t >= 1.0) {
        outColor = vec4(0.0);
        return;
    }

    vec2 uv = local * 0.5 + 0.5;
    float dist = length(uv - vec2(0.5));
    float random = hashNoise(uv, t, effectData.y);
    float random2 = mod(17.0 * random, 1.0);
    float strength = effectData.z * 100.0;
    float power = 1.0 - exp(-strength / 100.0);
    float phase = sin(PI * t);
    float alpha = smoothstep(0.4 * phase, 0.0, dist);

    vec3 innerColor = vec3(1.0, 237.0 / 255.0, 76.0 / 255.0);
    vec3 fastColor = vec3(1.0, 244.0 / 255.0, 186.0 / 255.0);
    innerColor = mix(innerColor, fastColor, power * 0.5 * abs(sin(1.2 * t)));
    vec3 outerColor = vec3(0.8 + 0.2 * power, 0.1 + 0.4 * power, 0.0);
    vec3 explosionColor = mix(outerColor, innerColor, alpha * 0.8);
    explosionColor = mix(explosionColor,
                         vec3(0.5 + random * 0.3,
                              0.5 + random * 0.3,
                              0.1 + 0.2 * random2 * power),
                         0.3);
    outColor = vec4(explosionColor, alpha);
}
