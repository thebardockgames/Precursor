#version 410 core


layout (location = 0) in vec3 xyz;
layout (location = 1) in vec2 st;

uniform vec4 u_color;

flat out vec4 fragment_color;
out vec2 tex_coord;

void main() {
  fragment_color = u_color;
  tex_coord = st;
  vec4 transformed = vec4(xyz, 1.0);

  // correct xy offset
  transformed.xy -= (2048.);
  // correct z scale
  transformed.z /= (8388608.0);
  transformed.z -= 1.0;
  // correct xy scale
  transformed.x /= (256.0);
  transformed.y /= -(128.0);
  transformed.y *= HEIGHT_SCALE;

  gl_Position = transformed;
}
