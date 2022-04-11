#version 330 core

precision mediump float;

layout(location = 0) in vec2 VERT_IN_POSITION;
layout(location = 1) in vec2 VERT_IN_TRANSLATE;
layout(location = 2) in vec2 VERT_IN_SCALE;

uniform vec2  CAMERA;
uniform vec2  WINDOW;
uniform float TIME_SECONDS;

#define OFFSET vec2(0.0f, 350.0f)

void main() {
    gl_Position =
        vec4((((VERT_IN_POSITION * VERT_IN_SCALE / 2.0f) + VERT_IN_TRANSLATE) -
              (CAMERA + OFFSET)) /
                 WINDOW,
             0.0f,
             1.0f);
}
