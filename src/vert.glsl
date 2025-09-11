#version 330 core

precision mediump float;

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 translate;
layout(location = 2) in vec2 scale;

uniform vec2  camera;
uniform vec2  window;
uniform float seconds;

void main() {
    gl_Position = vec4((((position * scale) + translate) - camera) / (window / 2.0f), 0.0f, 1.0f);
}
