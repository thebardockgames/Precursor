#version 410 core


out vec4 out_color;

uniform sampler2D tex;

in vec2 tex_coord;

void main() {
  vec2 texture_coords = vec2(tex_coord.x, tex_coord.y);
  out_color = vec4(0, 0, 0, 0);
  if (texture_coords.x < 0.0 || texture_coords.x > 1.0 || texture_coords.y > 1.0 || texture_coords.y < 0.0) {
    gl_FragDepth = 1.0;
  } else {
    gl_FragDepth = texture(tex, texture_coords).r;
  }
}
