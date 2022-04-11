#version 330 core

precision mediump float;

layout(location = 0) out vec4 FRAG_OUT_COLOR;

uniform vec2  WINDOW;
uniform float TIME_SECONDS;

void main() {
    FRAG_OUT_COLOR = vec4(gl_FragCoord.xy / WINDOW,
                          (sin(TIME_SECONDS) + 1.0f) / 2.0f,
                          1.0f);
}
