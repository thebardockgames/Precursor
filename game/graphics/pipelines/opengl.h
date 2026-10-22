#pragma once

/*!
 * @file opengl.h
 * OpenGL includes.
 */

#define GLFW_INCLUDE_NONE
#include <mutex>

#include "game/graphics/display.h"
#include "game/graphics/gfx.h"

#ifdef __SWITCH__
// No open SDL3 backend exists for Switch homebrew - see the development notes. Uses devkitPro's
// community SDL2 fork instead, hence the SDL2-vs-SDL3 branches throughout opengl.cpp.
#include <SDL.h>
#else
#include "third-party/SDL/include/SDL3/SDL.h"
#endif

#ifdef __ANDROID__
// Android's EGL only ever exposes EGL_OPENGL_ES_API (GLES), never desktop GL - unlike Switch,
// where devkitPro's Mesa/nouveau stack happens to expose real desktop GL. This project's renderer
// targets GLES 3.2 on Android (see the development notes' "Android GLES 3.2 renderer port" section for
// the full feature-compatibility audit that justified this over Vulkan or GL-over-Vulkan/Zink).
// Real GLES entry points are directly exported, linkable symbols in libGLESv3.so (unlike desktop
// GL, which needs a runtime loader like glad for anything past GL 1.1) - so no glad equivalent is
// needed here at all, just the real NDK headers.
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>  // for GL_TEXTURE_MAX_ANISOTROPY_EXT and friends

// GLES has no double-precision glClearDepth() - only the float glClearDepthf(). Aliased (rather
// than touching every call site) the same way game/system/hid/sdl2_compat.h aliases SDL2/SDL3
// naming differences for Switch.
#define glClearDepth(d) glClearDepthf((GLfloat)(d))

// Anisotropic filtering is core in desktop GL 4.6+ (and available earlier via
// GL_ARB_texture_filter_anisotropic, which is what this project's vendored glad.h was generated
// against), but isn't core in GLES at all - only via the GL_EXT_texture_filter_anisotropic
// extension (near-universal on real Android GPUs, unlike most other GLES extensions), which uses
// the _EXT-suffixed enum names instead. Aliased rather than touching every call site.
#define GL_MAX_TEXTURE_MAX_ANISOTROPY GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY GL_TEXTURE_MAX_ANISOTROPY_EXT

// GLES's KHR_debug (core since 3.2) uses the GL_APIENTRY calling-convention macro (underscore);
// desktop GL's KHR_debug extension names the same thing GLAPIENTRY (no underscore) - same
// concept, different macro name, aliased rather than touching the one callback that uses it.
#define GLAPIENTRY GL_APIENTRY

// GLES has no glMultiDrawElements() at all (single-draw glDrawElements only) - every real call
// site already has a single-draw fallback path behind a `no_multidraw` flag (BucketRenderer.h
// defaults this true on Android, avoiding the loop below at runtime), but the multidraw branch
// still needs to *compile* even though it's dead code here. A same-signature shim, rather than
// touching Shrub.cpp/TFragment.cpp/Tie3.cpp's 7 call sites, keeps this a one-place fix.
inline void glMultiDrawElements(GLenum mode,
                                const GLsizei* count,
                                GLenum type,
                                const void* const* indices,
                                GLsizei drawcount) {
  for (GLsizei i = 0; i < drawcount; i++) {
    glDrawElements(mode, count[i], type, indices[i]);
  }
}
#else
#include "third-party/glad/include/glad/glad.h"
#endif

class GLDisplay : public GfxDisplay {
 public:
  GLDisplay(SDL_Window* window, SDL_GLContext gl_context, bool is_main);
  virtual ~GLDisplay();

  // Overrides
  std::shared_ptr<DisplayManager> get_display_manager() const override { return m_display_manager; }
  std::shared_ptr<InputManager> get_input_manager() const override { return m_input_manager; }

  void render() override;

 private:
  SDL_Window* m_window;
  SDL_GLContext m_gl_context;

  std::shared_ptr<DisplayManager> m_display_manager;
  std::shared_ptr<InputManager> m_input_manager;

  bool m_should_quit = false;
  bool m_take_screenshot_next_frame = false;
  void process_sdl_events();

  struct DisplayState {
    // move it a bit away from the top by default
    s32 window_pos_x = 50;
    s32 window_pos_y = 50;
    int window_size_width = 640;
    int window_size_height = 480;
    float window_scale_x = 1.f;
    float window_scale_y = 1.f;

    bool pending_size_change = false;
    s32 requested_size_width = 0;
    s32 requested_size_height = 0;
  } m_display_state, m_display_state_copy;
  std::mutex m_lock;

  struct {
    bool pending = false;
    int width = 0;
    int height = 0;
  } m_pending_size;

  // splash screen
  GLuint m_splash_texture = 0;
  GLuint m_splash_program = 0;
  GLuint m_splash_vao = 0;
  void init_splash();
  void draw_splash(int fb_w, int fb_h);
};

extern const GfxRendererModule gRendererOpenGL;
