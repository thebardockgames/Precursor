#version 410 core
// Debug shader for drawing things in red. Uses the same conventions as direct_basic, see there for more details


layout (location = 0) in vec3 position_in;

out vec4 fragment_color;

void main() {
  gl_Position = vec4((position_in.x - 0.5) * 16., -(position_in.y - 0.5) * 32.0, position_in.z * 2.0 - 1., 1.0);
  // scissoring area adjust
  gl_Position.y *= SCISSOR_ADJUST;
  fragment_color = vec4(1.0, 0, 0, 0.7);
}
