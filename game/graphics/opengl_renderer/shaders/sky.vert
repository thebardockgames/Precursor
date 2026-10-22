#version 410 core


layout (location = 0) in vec3 position_in;
layout (location = 1) in vec4 rgba_in;
layout (location = 2) in vec3 tex_coord_in;

out vec4 fragment_color;
// `noperspective` doesn't exist in GLES core (no universal extension either - the one that adds
// it, GL_NV_shader_noperspective_interpolation, is Tegra/NVIDIA-only, not available on the
// Adreno/Mali/PowerVR GPUs actually found in real Android phones). Safe to drop entirely, not an
// approximation: gl_Position.w below is a hardcoded 1.0, which makes GL's normal perspective-
// correct interpolation (dividing by w) a mathematical no-op - identical to `noperspective` for
// this specific shader.
out vec3 tex_coord;

void main() {
  gl_Position = vec4((position_in.x - 0.5) * 16. , -(position_in.y - 0.5) * 32.0, position_in.z * 2.0 - 1., 1.0);
  // scissoring area adjust
  gl_Position.y *= SCISSOR_ADJUST;
  fragment_color = vec4(rgba_in.x, rgba_in.y, rgba_in.z, rgba_in.a * 2.0);
  tex_coord = tex_coord_in;
}
