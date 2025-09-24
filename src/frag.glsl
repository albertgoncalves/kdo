#version 330 core

layout(location = 0) out vec4 pixel;

uniform vec2  window;
uniform float time;
uniform bool  paused;

#define PI 3.14159274f

void main() {
    // NOTE: See `http://the-witness.net/news/2022/02/a-shader-trick/`.
    vec3 color = mix(vec3(gl_FragCoord.xy / window, (sin(0.1f * time * 2.0f * PI) + 1.0f) / 2.0f),
                     vec3(1.0f),
                     0.625f);
    if (paused) {
        color = vec3(1.0f) - color;
    }
    pixel = vec4(color, 1.0f);
}
