#version 330 core

precision mediump float;

layout(location = 0) out vec4 FRAG_OUT_COLOR;

uniform vec2  WINDOW;
uniform float TIME_SECONDS;
uniform bool  PAUSED;

#define PI 3.14159274f

void main() {
    // NOTE: See `http://the-witness.net/news/2022/02/a-shader-trick/`.
    vec3 color =
        mix(vec3(gl_FragCoord.xy / WINDOW,
                 (sin(0.1f * TIME_SECONDS * 2.0f * PI) + 1.0f) / 2.0f),
            vec3(1.0f),
            0.625f);
    if (PAUSED) {
        color = vec3(1.0f) - color;
    }
    FRAG_OUT_COLOR = vec4(color, 1.0f);
}
