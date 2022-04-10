#version 330 core

precision mediump float;

layout(location = 0) in vec2 VERT_IN_POSITION;
layout(location = 1) in vec2 VERT_IN_TRANSLATE;
layout(location = 2) in vec2 VERT_IN_SCALE;

uniform vec2  CAMERA;
uniform vec2  WINDOW;
uniform float TIME_SECONDS;

#define ZOOM   60
#define OFFSET vec2(0.0, 0.375f)

void main() {
    gl_Position =
        vec4((VERT_IN_TRANSLATE +
              ((VERT_IN_POSITION.xy * VERT_IN_SCALE * ZOOM) / WINDOW)) -
                 vec2(CAMERA + OFFSET),
             0.0,
             1.0);
}
