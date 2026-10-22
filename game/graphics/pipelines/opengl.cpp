/*!
 * @file opengl.cpp
 * Lower-level OpenGL interface. No actual rendering is performed here!
 */

#include "opengl.h"
#include "game/system/hid/android_touch_controls.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#ifdef __ANDROID__
#include <unistd.h>
#endif

#include "common/dma/dma_copy.h"
#include "common/global_profiler/GlobalProfiler.h"
#include "common/goal_constants.h"
#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "common/util/FrameLimiter.h"
#include "common/util/Timer.h"
#include "common/util/compress.h"

#include "game/graphics/display.h"
#include "game/graphics/gfx.h"
#include "game/graphics/opengl_renderer/OpenGLRenderer.h"
#include "game/graphics/opengl_renderer/debug_gui.h"
#include "game/graphics/screenshot.h"
#include "game/graphics/texture/TexturePool.h"
#include "game/runtime.h"
#include "game/sce/libscf.h"
#include "game/system/hid/input_manager.h"
#include "game/system/hid/sdl_util.h"

#include "fmt/format.h"
#ifdef __SWITCH__
#include <SDL.h>

#include "third-party/imgui/imgui_impl_sdl2.h"
#else
#include "third-party/SDL/include/SDL3/SDL.h"
#include "third-party/SDL/include/SDL3/SDL_hints.h"
#include "third-party/SDL/include/SDL3/SDL_version.h"
#include "third-party/imgui/imgui_impl_sdl3.h"
#endif
#include "third-party/imgui/imgui.h"
#include "third-party/imgui/imgui_freetype.h"
#include "third-party/imgui/imgui_impl_opengl3.h"
#include "third-party/imgui/imgui_style.h"
#define STBI_WINDOWS_UTF8
#include "common/util/dialogs.h"
#include "common/util/string_util.h"

#include "third-party/stb_image/stb_image.h"

constexpr bool run_dma_copy = false;

constexpr PerGameVersion<int> fr3_level_count(jak1::LEVEL_TOTAL,
                                              jak2::LEVEL_TOTAL,
                                              jak3::LEVEL_TOTAL,
                                              jakx::LEVEL_TOTAL);

struct GraphicsData {
  // vsync
  std::mutex sync_mutex;
  std::condition_variable sync_cv;

  // dma chain transfer
  std::mutex dma_mutex;
  std::condition_variable dma_cv;
  u64 frame_idx = 0;
  u64 frame_idx_of_input_data = 0;
  bool has_data_to_render = false;
  FixedChunkDmaCopier dma_copier;

  // texture pool
  std::shared_ptr<TexturePool> texture_pool;

  std::shared_ptr<Loader> loader;

  // temporary opengl renderer
  OpenGLRenderer ogl_renderer;

  OpenGlDebugGui debug_gui;

  FrameLimiter frame_limiter;
  Timer engine_timer;
  double last_engine_time = 1. / 60.;
  float pmode_alp = 1.f;

  std::string imgui_log_filename, imgui_filename;
  GameVersion version;

  GraphicsData(GameVersion version)
      : dma_copier(EE_MAIN_MEM_SIZE),
        texture_pool(std::make_shared<TexturePool>(version)),
        loader(std::make_shared<Loader>(
            file_util::get_jak_project_dir() / "out" / game_version_names[version] / "fr3",
            fr3_level_count[version])),
        ogl_renderer(texture_pool, loader, version),
        debug_gui(),
        version(version) {}
};

std::unique_ptr<GraphicsData> g_gfx_data;

static bool gl_inited = false;

// A failed window/context creation is fatal. On desktop the message names the desktop GL version;
// on Android tinyfiledialogs has no UI and the game kept running into a SIGSEGV, so show a native
// SDL message box with the real requirement (OpenGL ES 3.2) and exit cleanly. (AI-assisted)
static void report_display_failure(const char* desktop_message) {
#ifdef __ANDROID__
  lg::error("Display initialization failed: OpenGL ES 3.2 is required");
  SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR, "Precursor",
      "Tu teléfono no pudo crear el contexto OpenGL ES 3.2 que necesita el juego.\n\n"
      "Your device could not create the OpenGL ES 3.2 context this game needs.",
      nullptr);
  lg::finish();
  _exit(3);
#else
  dialogs::create_error_message_dialog("Critical Error Encountered", desktop_message);
#endif
}
static int gl_init(GfxGlobalSettings& settings) {
  prof().instant_event("ROOT");
  Timer gl_init_timer;
  // Initialize SDL
  {
    auto p = scoped_prof("startup::sdl::init_sdl");
    // remove SDL garbage from hooking signal handler.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#ifdef __SWITCH__
    // SDL2's SDL_Init returns 0 on success, negative on failure (SDL3 flipped this to bool).
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#else
    if (!SDL_Init(SDL_INIT_VIDEO)) {
#endif
      sdl_util::log_error("Could not initialize SDL, exiting");
      dialogs::create_error_message_dialog("Critical Error Encountered",
                                           "Could not initialize SDL, exiting");
      return 1;
    }
  }

  {
    auto p = scoped_prof("startup::sdl::get_version_info");

#ifdef __SWITCH__
    // SDL2's version query is out-param based, unlike SDL3's int-returning SDL_VERSION/GetVersion.
    SDL_version compiled_sdl_version;
    SDL_VERSION(&compiled_sdl_version);
    SDL_version linked_sdl_version;
    SDL_GetVersion(&linked_sdl_version);
    lg::info("SDL Initialized, compiled with version - {}.{}.{} | linked with version - {}.{}.{}",
             compiled_sdl_version.major, compiled_sdl_version.minor, compiled_sdl_version.patch,
             linked_sdl_version.major, linked_sdl_version.minor, linked_sdl_version.patch);
#else
    auto compiled_sdl_version = SDL_VERSION;
    auto linked_sdl_version = SDL_GetVersion();
    lg::info("SDL Initialized, compiled with version - {} | linked with version - {}",
             compiled_sdl_version, linked_sdl_version);
#endif
  }

  {
    auto p = scoped_prof("startup::sdl::set_gl_attributes");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __ANDROID__
    // Android's EGL only ever exposes EGL_OPENGL_ES_API, never desktop GL (see opengl.h) - request
    // a real GLES context instead of a desktop core profile. GLES 3.2 was chosen after auditing
    // every GL feature this renderer actually uses (not just the nominal "needs GL 4.1/4.3" - see
    // the development notes' "Android GLES 3.2 renderer port" section) - nothing here needs anything
    // past GLES 3.2 (no geometry/tessellation/compute shaders, no bindless textures, no
    // multi-draw-indirect), and 3.2 is essentially universal on real Android hardware (API 24+,
    // ~2016 onward).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    if (settings.debug) {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    } else {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
#ifndef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  }
  lg::info("gl init took {:.3f}s\n", gl_init_timer.getSeconds());
  return 0;
}

static void gl_exit() {
  g_gfx_data.reset();
  gl_inited = false;
}

static void init_imgui(SDL_Window* window,
                       SDL_GLContext gl_context,
                       const std::string& glsl_version) {
  // check that version of the library is okay
  IMGUI_CHECKVERSION();

  // this does initialization for stuff like the font data
  ImGui::CreateContext();

  // Init ImGui settings
  g_gfx_data->imgui_filename = file_util::get_file_path({"imgui.ini"});
  g_gfx_data->imgui_log_filename = file_util::get_file_path({"imgui_log.txt"});
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  io.IniFilename = g_gfx_data->imgui_filename.c_str();
  io.LogFilename = g_gfx_data->imgui_log_filename.c_str();

  if (Gfx::g_debug_settings.alternate_style) {
    ImGui::applyAlternateStyle();
  }

  ImGui::applyFontStyle();

  // set up to get inputs for this window
#ifdef __SWITCH__
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
#else
  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
#endif

  // NOTE: imgui's setup calls functions that may fail intentionally, and attempts to disable error
  // reporting so these errors are invisible. But it does not work, and some weird X11 default
  // cursor error is set here that we clear.
  SDL_ClearError();

  // set up the renderer
  // An empty glsl_version means "let imgui_impl_opengl3.cpp auto-detect its own GLSL version" -
  // it specifically checks for a real nullptr, not just an empty C-string, to trigger that path
  // (see its ImGui_ImplOpenGL3_Init()), so this can't just forward glsl_version.c_str() unchanged.
  ImGui_ImplOpenGL3_Init(glsl_version.empty() ? nullptr : glsl_version.c_str());
}

static std::shared_ptr<GfxDisplay> gl_make_display(int width,
                                                   int height,
                                                   const char* title,
                                                   GfxGlobalSettings& /*settings*/,
                                                   GameVersion game_version,
                                                   bool is_main) {
  // Setup the window
  prof().instant_event("ROOT");
  prof().begin_event("startup::sdl::create_window");
#ifdef __SWITCH__
  // SDL2's SDL_CreateWindow takes explicit x/y position (SDL3 dropped that param) and has no
  // per-window HIGH_PIXEL_DENSITY concept (moot on Switch's fixed-resolution displays anyway).
  SDL_Window* window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#else
  SDL_Window* window =
      SDL_CreateWindow(title, width, height,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif
  prof().end_event();
  if (!window) {
    sdl_util::log_error("gl_make_display failed - Could not create display window");
    report_display_failure(
        "Unable to create OpenGL window.\nOpenGOAL requires OpenGL 4.3.\nEnsure your GPU "
        "supports this and your drivers are up to date.");
    return NULL;
  }

  // Make an OpenGL Context
  prof().begin_event("startup::sdl::create_context");
  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  prof().end_event();
  if (!gl_context) {
    sdl_util::log_error("gl_make_display failed - Could not create OpenGL Context");
    report_display_failure(
        "Unable to create OpenGL context.\nOpenGOAL requires OpenGL 4.3.\nEnsure your GPU "
        "supports this and your drivers are up to date.");
    return NULL;
  }

  {
    auto p = scoped_prof("startup::sdl::assign_context");
#ifdef __SWITCH__
    // SDL2's SDL_GL_MakeCurrent returns 0 on success, negative on failure (SDL3 uses bool).
    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
#else
    if (!SDL_GL_MakeCurrent(window, gl_context)) {
#endif
      sdl_util::log_error("gl_make_display failed - Could not associated context with window");
      report_display_failure(
          "Unable to create OpenGL window with context.\nOpenGOAL requires OpenGL 4.3.\nEnsure "
          "your GPU supports this and your drivers are up to date.");
      return NULL;
    }
  }

  if (!gl_inited) {
    {
      auto p = scoped_prof("startup::sdl::glad_init");
#ifdef __ANDROID__
      // No loader needed at all on Android - real GLES entry points are directly exported,
      // linkable symbols in libGLESv3.so (see game/graphics/pipelines/opengl.h's __ANDROID__
      // branch), unlike desktop GL which needs glad/eglGetProcAddress for anything past GL 1.1.
      if (false) {
#elif defined(__SWITCH__)
      // gladLoadGL() (no args) unconditionally calls glad's own internal dlopen()-based loader
      // (open_gl() in third-party/glad/src/glad.c), which doesn't exist on Switch. Redundant
      // anyway once gladLoadGLLoader() has already been called with an explicit loader function -
      // its own return value already tells us whether loading succeeded.
      if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
#else
      gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
      if (!gladLoadGL()) {
#endif
        lg::error("GL init fail");
        dialogs::create_error_message_dialog("Critical Error Encountered",
                                             "Unable to initialize OpenGL API.\nOpenGOAL requires "
                                             "OpenGL 4.3.\nEnsure your GPU "
                                             "supports this and your drivers are up to date.");
        return NULL;
      }
    }
    {
      auto p = scoped_prof("startup::sdl::gfx_data_init");
      g_gfx_data = std::make_unique<GraphicsData>(game_version);
    }
    gl_inited = true;
    const char* gl_version = (const char*)glGetString(GL_VERSION);
#ifdef __ANDROID__
    // No glad on Android (see above) - no GLVersion global to read, just print the raw
    // GL_VERSION string (GLES reports it as e.g. "OpenGL ES 3.2 <driver info>").
    lg::info("OpenGL ES initialized - {}", gl_version);
#else
    lg::info("OpenGL initialized - v{}.{} | Renderer: {}", GLVersion.major, GLVersion.minor,
             gl_version);
#endif
  }

  {
    auto p = scoped_prof("startup::sdl::window_extras");
    float dpi = 1.0f;
#ifndef __SWITCH__
    // No per-window display-scale query in SDL2, and no meaningful concept of it on Switch's
    // fixed-resolution displays anyway - just use the default of 1.0f there.
    float display_scale = SDL_GetWindowDisplayScale(window);
    if (display_scale > 0.0) {
      dpi = display_scale * 96.0f;
    }
#endif

    // Setup Window Icon
    const auto image_path = file_util::get_jak_project_dir() / "game" / "assets" /
                            version_to_game_name(game_version) /
                            (dpi == 1.0f ? "app64.png" : "app256.png");
    if (fs::exists(image_path)) {
      int icon_width;
      int icon_height;

      auto icon_data = stbi_load(image_path.string().c_str(), &icon_width, &icon_height, nullptr,
                                 STBI_rgb_alpha);
      if (icon_data) {
#ifdef __SWITCH__
        // SDL2's equivalent of SDL_CreateSurfaceFrom takes a bit-depth instead of a pixel format
        // enum, and frees with SDL_FreeSurface instead of SDL_DestroySurface.
        SDL_Surface* icon_surf = SDL_CreateRGBSurfaceWithFormatFrom(
            (void*)icon_data, icon_width, icon_height, 32, 4 * icon_width,
            SDL_PIXELFORMAT_RGBA32);
#else
        SDL_Surface* icon_surf = SDL_CreateSurfaceFrom(
            icon_width, icon_height, SDL_PIXELFORMAT_RGBA32, (void*)icon_data, 4 * icon_width);
#endif
        if (!icon_surf) {
          sdl_util::log_error("unable to generate surface from app icon data");
        } else {
          SDL_SetWindowIcon(window, icon_surf);
#ifdef __SWITCH__
          SDL_FreeSurface(icon_surf);
#else
          SDL_DestroySurface(icon_surf);
#endif
        }
        stbi_image_free(icon_data);
      } else {
        lg::error("Could not load icon for OpenGL window, couldn't load image data");
      }
    } else {
      lg::error("Could not load icon for OpenGL window, {} does not exist", image_path.string());
    }
  }

  prof().begin_event("startup::sdl::create_GLDisplay");
  auto display = std::make_shared<GLDisplay>(window, gl_context, is_main);
  display->set_imgui_visible(Gfx::g_debug_settings.show_imgui);
  prof().end_event();

  {
    auto p = scoped_prof("startup::sdl::init_imgui");
    // setup imgui
#ifdef __ANDROID__
    // Passing a real GLSL version string here always overrides imgui_impl_opengl3.cpp's own
    // IMGUI_IMPL_OPENGL_ES3 auto-detection (which otherwise picks the correct "#version 300 es"
    // itself - see that file's ImGui_ImplOpenGL3_Init()) - the desktop "#version 430" string
    // below was being passed unconditionally even on Android, and GLES's compiler flatly rejects
    // "430" as an unknown version (confirmed via a real crash on real hardware: SIGABRT from
    // imgui's own assert after ImGui_ImplOpenGL3_CreateDeviceObjects() failed to compile with
    // that string). Passing nullptr here restores the auto-detection this project's other
    // Android-specific ES3 wiring (game/CMakeLists.txt's IMGUI_IMPL_OPENGL_ES3 auto-select
    // comment) already assumed was in effect.
    init_imgui(window, gl_context, "");
#elif __APPLE__
    init_imgui(window, gl_context, "#version 410");
#else
    init_imgui(window, gl_context, "#version 430");
#endif
  }

  return std::static_pointer_cast<GfxDisplay>(display);
}

GLDisplay::GLDisplay(SDL_Window* window, SDL_GLContext gl_context, bool is_main)
    : m_window(window),
      m_gl_context(gl_context),
      m_display_manager(std::make_shared<DisplayManager>(window)),
      m_input_manager(std::make_shared<InputManager>(window)) {
  m_main = is_main;
  m_display_manager->set_input_manager(m_input_manager);
  // Register commands
  m_input_manager->register_command(
      CommandBinding::Source::KEYBOARD,
      CommandBinding(Gfx::g_debug_settings.hide_imgui_key, [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0) {
          if (!Gfx::g_debug_settings.ignore_hide_imgui) {
            set_imgui_visible(!is_imgui_visible());
          }
        }
      }));
  ;
  m_input_manager->register_command(
      CommandBinding::Source::KEYBOARD,
      CommandBinding(SDLK_F2, [&]() { m_take_screenshot_next_frame = true; }));

  const auto& bind = Gfx::g_debug_settings.toggle_fullscreen_key;

  m_input_manager->register_command(
      CommandBinding::Source::KEYBOARD,
      CommandBinding(bind.key, bind.modifiers, [&](const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0 &&
            bind.modifiers.has_necessary_modifiers(SDL_GetModState())) {
          m_display_manager->toggle_display_mode();
        }
      }));
}

void GLDisplay::init_splash() {
  if (m_splash_program)
    return;
  if (!Gfx::g_splash.ready.load())
    return;

  auto shader_folder = "game/graphics/opengl_renderer/shaders";
  auto vert_src =
      file_util::read_text_file(file_util::get_file_path({shader_folder, "splash.vert"}));
  auto frag_src =
      file_util::read_text_file(file_util::get_file_path({shader_folder, "splash.frag"}));

#ifdef __ANDROID__
  // Same runtime `#version 410 core` -> GLSL ES substitution as Shader.cpp - this function reads
  // its shader source directly instead of going through the Shader class, so it needs its own
  // copy of the substitution. See Shader.cpp for the full rationale.
  static const std::string kGlesHeader =
      "#version 320 es\n"
      "precision highp float;\n"
      "precision highp int;\n"
      "precision highp sampler2D;\n"
      "precision highp sampler2DArray;\n"
      "precision highp samplerCube;\n"
      "precision highp sampler2DShadow;\n"
      "precision highp sampler3D;\n";
  vert_src = std::regex_replace(vert_src, std::regex("#version 410 core"), kGlesHeader);
  frag_src = std::regex_replace(frag_src, std::regex("#version 410 core"), kGlesHeader);
#endif

  constexpr int len = 1024;
  GLint compile_ok;
  char err[len];

  auto compile_shader = [](GLenum type, const char* src, GLint& compile_ok, char* err) -> GLuint {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &compile_ok);
    if (!compile_ok) {
      glGetShaderInfoLog(s, len, nullptr, err);
      lg::error("splash shader compile failed: {}\n", err);
      glDeleteShader(s);
      return 0;
    }
    return s;
  };

  GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src.c_str(), compile_ok, err);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src.c_str(), compile_ok, err);
  if (!vs || !fs) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return;
  }

  m_splash_program = glCreateProgram();
  glAttachShader(m_splash_program, vs);
  glAttachShader(m_splash_program, fs);
  glLinkProgram(m_splash_program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  glGetProgramiv(m_splash_program, GL_LINK_STATUS, &compile_ok);
  if (!compile_ok) {
    glGetProgramInfoLog(m_splash_program, len, nullptr, err);
    lg::error("Failed to link splash shader:\n{}", err);
    glDeleteProgram(m_splash_program);
    m_splash_program = 0;
    return;
  }

  if (!Gfx::g_splash.data.empty()) {
    glGenTextures(1, &m_splash_texture);
    glBindTexture(GL_TEXTURE_2D, m_splash_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Gfx::g_splash.width, Gfx::g_splash.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, Gfx::g_splash.data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    Gfx::g_splash.data.clear();
    Gfx::g_splash.data.shrink_to_fit();
  }

  glGenVertexArrays(1, &m_splash_vao);
}

void GLDisplay::draw_splash(int fb_w, int fb_h) {
  if (!m_splash_program || !m_splash_texture)
    return;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, fb_w, fb_h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glUseProgram(m_splash_program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_splash_texture);
  glUniform1i(glGetUniformLocation(m_splash_program, "splash_tex"), 0);
  glUniform2f(glGetUniformLocation(m_splash_program, "u_res"), fb_w, fb_h);
  glUniform2f(glGetUniformLocation(m_splash_program, "u_tex"), Gfx::g_splash.width,
              Gfx::g_splash.height);
  glBindVertexArray(m_splash_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

GLDisplay::~GLDisplay() {
  if (m_splash_texture)
    glDeleteTextures(1, &m_splash_texture);
  if (m_splash_program)
    glDeleteProgram(m_splash_program);
  if (m_splash_vao)
    glDeleteVertexArrays(1, &m_splash_vao);
  // Cleanup ImGUI
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ImGui_ImplOpenGL3_Shutdown();
#ifdef __SWITCH__
  ImGui_ImplSDL2_Shutdown();
#else
  ImGui_ImplSDL3_Shutdown();
#endif
  ImGui::DestroyContext();
  // Cleanup SDL
#ifdef __SWITCH__
  // SDL3 renamed SDL_GL_DeleteContext to SDL_GL_DestroyContext.
  SDL_GL_DeleteContext(m_gl_context);
#else
  SDL_GL_DestroyContext(m_gl_context);
#endif
  SDL_DestroyWindow(m_window);
  // cleanup SDL related sub-systems before we quit SDL
  if (m_display_manager) {
    m_display_manager.reset();
  }
  if (m_input_manager) {
    m_input_manager.reset();
  }
  // now quit SDL
  SDL_Quit();
  if (m_main) {
    gl_exit();
  }
}

void render_game_frame(int game_width,
                       int game_height,
                       int window_fb_width,
                       int window_fb_height,
                       int draw_region_width,
                       int draw_region_height,
                       int msaa_samples,
                       int brightness_contrast_color,
                       int brightness_contrast_alpha,
                       bool take_screenshot) {
  // wait for a copied chain.
  bool got_chain = false;
  {
    auto p = scoped_prof("wait-for-dma");
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    // there's a timeout here, so imgui can still be responsive even if we don't render anything
    got_chain = g_gfx_data->dma_cv.wait_for(lock, std::chrono::milliseconds(40),
                                            [=] { return g_gfx_data->has_data_to_render; });
  }
  // render that chain.
  if (got_chain) {
#ifdef __ANDROID__
    android_touch_controls::game_frame_rendered();
#endif
    g_gfx_data->frame_idx_of_input_data = g_gfx_data->frame_idx;
    RenderOptions options;
    options.game_res_w = game_width;
    options.game_res_h = game_height;
    options.window_framebuffer_width = window_fb_width;
    options.window_framebuffer_height = window_fb_height;
    options.draw_region_width = draw_region_width;
    options.draw_region_height = draw_region_height;
    options.msaa_samples = msaa_samples;
    options.brightness_contrast_color = brightness_contrast_color;
    options.brightness_contrast_alpha = brightness_contrast_alpha;
    options.draw_render_debug_window = g_gfx_data->debug_gui.should_draw_render_debug();
    options.draw_profiler_window = g_gfx_data->debug_gui.should_draw_profiler();
    options.draw_loader_window = g_gfx_data->debug_gui.should_draw_loader_menu();
    options.draw_subtitle_editor_window = g_gfx_data->debug_gui.should_draw_subtitle_editor();
    options.draw_filters_window = g_gfx_data->debug_gui.should_draw_filters_menu();
    options.save_screenshot = false;
    options.quick_screenshot = false;
    options.internal_res_screenshot = false;
    options.gpu_sync = g_gfx_data->debug_gui.should_gl_finish();

    if (take_screenshot) {
      options.save_screenshot = true;
      options.quick_screenshot = true;
      options.screenshot_path = file_util::make_screenshot_filepath(g_game_version);
    }
    // note : it's important we call get_screenshot_flag first because it modifies state
    if (g_gfx_data->debug_gui.get_screenshot_flag() || g_want_screenshot) {
      g_want_screenshot = false;
      options.save_screenshot = true;
      options.internal_res_screenshot = true;
      options.game_res_w = g_screen_shot_settings->width;
      options.game_res_h = g_screen_shot_settings->height;
      options.window_framebuffer_width = options.game_res_w;
      options.window_framebuffer_height = options.game_res_h;
      options.draw_region_width = options.game_res_w;
      options.draw_region_height = options.game_res_h;
      options.msaa_samples = g_screen_shot_settings->msaa;
      options.screenshot_path =
          file_util::make_screenshot_filepath(g_game_version, get_screen_shot_name());
    }

    options.draw_small_profiler_window =
        g_gfx_data->debug_gui.master_enable && g_gfx_data->debug_gui.small_profiler;
    options.pmode_alp_register = g_gfx_data->pmode_alp;

    GLint msaa_max;
    glGetIntegerv(GL_MAX_SAMPLES, &msaa_max);
    if (options.msaa_samples > msaa_max) {
      options.msaa_samples = msaa_max;
    }
    // A corrupt sample count must not take the whole game down in make_fbo. (AI-assisted)
    if (options.msaa_samples < 1) {
      lg::warn("render_game_frame: invalid msaa {}, using 1", options.msaa_samples);
      options.msaa_samples = 1;
    }

    if constexpr (run_dma_copy) {
      auto& chain = g_gfx_data->dma_copier.get_last_result();
      g_gfx_data->ogl_renderer.render(DmaFollower(chain.data.data(), chain.start_offset), options);
    } else {
      auto p = scoped_prof("ogl-render");
      g_gfx_data->ogl_renderer.render(DmaFollower(g_gfx_data->dma_copier.get_last_input_data(),
                                                  g_gfx_data->dma_copier.get_last_input_offset()),
                                      options);
    }
  }

  // before vsync, mark the chain as rendered.
  {
    // should be fine to remove this mutex if the game actually waits for vsync to call
    // send_chain again. but let's be safe for now.
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    g_gfx_data->engine_timer.start();
    g_gfx_data->has_data_to_render = false;
    g_gfx_data->sync_cv.notify_all();
  }
}

void GLDisplay::process_sdl_events() {
  SDL_Event evt;
  while (SDL_PollEvent(&evt) != 0) {
#ifdef __SWITCH__
    // SDL3 renamed SDL_QUIT to SDL_EVENT_QUIT (see game/system/hid/sdl2_event_compat.h for the
    // rest of the SDL2/SDL3 event-name mapping used by the hid/ code).
    if (evt.type == SDL_QUIT) {
#else
    if (evt.type == SDL_EVENT_QUIT) {
#endif
      m_should_quit = true;
    }
    {
      auto p = scoped_prof("sdl-display-manager");
      m_display_manager->process_sdl_event(evt);
    }
    if (!m_should_quit) {
      {
        auto p = scoped_prof("imgui-sdl-process");
#ifdef __SWITCH__
        ImGui_ImplSDL2_ProcessEvent(&evt);
#else
        ImGui_ImplSDL3_ProcessEvent(&evt);
#endif
      }
    }
    {
      auto p = scoped_prof("sdl-input-monitor-process-event");
      m_input_manager->process_sdl_event(evt);
    }
  }
}

/*!
 * Main function called to render graphics frames. This is called in a loop.
 */
void GLDisplay::render() {
  // Before we process the current frames SDL events we for keyboard/mouse button inputs.
  //
  // This technically means that keyboard/mouse button inputs will be a frame behind but the
  // event-based code is limiting (there aren't enough events to achieve a totally stateless
  // approach). Binding handling is still taken care of by the event code though.
  {
    auto p = scoped_prof("sdl-input-monitor-poll-for-kb-mouse");
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) {
      m_input_manager->clear_keyboard_actions();
    } else {
      m_input_manager->poll_keyboard_data();
    }
    if (io.WantCaptureMouse) {
      m_input_manager->clear_mouse_actions();
    } else {
      m_input_manager->poll_mouse_data();
    }
    m_input_manager->finish_polling();
  }
  // Now process SDL Events
  process_sdl_events();
  // Also process any display related events received from the EE (the game)
  // this is done here so they run from the perspective of the graphics thread
  {
    auto p = scoped_prof("display-manager-ee-events");
    m_display_manager->process_ee_events();
  }
  {
    auto p = scoped_prof("input-manager-ee-events");
    m_input_manager->process_ee_events();
  }

  // imgui start of frame
  {
    auto p = scoped_prof("imgui-new-frame");
    ImGui_ImplOpenGL3_NewFrame();
#ifdef __SWITCH__
    ImGui_ImplSDL2_NewFrame();
#else
    ImGui_ImplSDL3_NewFrame();
#endif
    ImGui::NewFrame();
  }

  // framebuffer size
  int fbuf_w, fbuf_h;
#ifdef __SWITCH__
  // SDL2 has no SDL_GetWindowSizeInPixels; SDL_GL_GetDrawableSize is the equivalent for a GL
  // window (both report the actual backbuffer size, which is what mattered here anyway).
  SDL_GL_GetDrawableSize(m_window, &fbuf_w, &fbuf_h);
#else
  SDL_GetWindowSizeInPixels(m_window, &fbuf_w, &fbuf_h);
#endif

  // render game!
  g_gfx_data->debug_gui.master_enable = is_imgui_visible();
  if (g_gfx_data->debug_gui.should_advance_frame()) {
    auto p = scoped_prof("game-render");
    int game_res_w = Gfx::g_global_settings.game_res_w;
    int game_res_h = Gfx::g_global_settings.game_res_h;
    if (game_res_w <= 0 || game_res_h <= 0) {
      // if the window size reports 0, the game will ask for a 0 sized window, and nothing likes
      // that.
      game_res_w = 640;
      game_res_h = 480;
    }
    // set the size of the visible/playable portion of the game in the window
    get_display_manager()->set_game_size(Gfx::g_global_settings.lbox_w,
                                         Gfx::g_global_settings.lbox_h);

    // draw splash screen on startup if requested
    if (SplashScreen && DiskBoot && !m_splash_program) {
      init_splash();
    }
    if (SplashScreen && Gfx::g_splash.ready.load() &&
        SplashTimer.getSeconds() < SPLASH_SCREEN_TIME) {
      draw_splash(fbuf_w, fbuf_h);
    }

    render_game_frame(
        game_res_w, game_res_h, fbuf_w, fbuf_h, Gfx::g_global_settings.lbox_w,
        Gfx::g_global_settings.lbox_h, Gfx::g_global_settings.msaa_samples,
        Gfx::g_global_settings.brightness_contrast_color,
        Gfx::g_global_settings.brightness_contrast_alpha,
        m_take_screenshot_next_frame && g_gfx_data->debug_gui.screenshot_hotkey_enabled);
    // If we took a screenshot, stop taking them now!
    if (m_take_screenshot_next_frame) {
      m_take_screenshot_next_frame = false;
    }
  }

  // render debug
  if (is_imgui_visible()) {
    auto p = scoped_prof("debug-gui");
    g_gfx_data->debug_gui.draw(g_gfx_data->dma_copier.get_last_result().stats);
  }
  {
    auto p = scoped_prof("imgui-render");
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  // actual vsync
  g_gfx_data->debug_gui.finish_frame();
  if (Gfx::g_global_settings.framelimiter) {
    auto p = scoped_prof("frame-limiter");
    g_gfx_data->frame_limiter.run(
        Gfx::g_global_settings.target_fps, Gfx::g_global_settings.experimental_accurate_lag,
        Gfx::g_global_settings.sleep_in_frame_limiter, g_gfx_data->last_engine_time);
  }

  {
    auto p = scoped_prof("swap-buffers");
    SDL_GL_SwapWindow(m_window);
#ifdef __ANDROID__
    android_touch_controls::frame_presented();
#endif
  }

  // actually wait for vsync
  if (g_gfx_data->debug_gui.should_gl_finish()) {
    glFinish();
  }

  // switch vsync modes, if requested
  if (Gfx::g_global_settings.vsync != Gfx::g_global_settings.old_vsync) {
    Gfx::g_global_settings.old_vsync = Gfx::g_global_settings.vsync;
    // NOTE - -1 can be used for adaptive vsync, maybe useful for Jak 2+?
    // https://wiki.libsdl.org/SDL3/SDL_GL_SetSwapInterval
    SDL_GL_SetSwapInterval(Gfx::g_global_settings.vsync);
  }

  // Start timing for the next frame.
  g_gfx_data->debug_gui.start_frame();
  prof().instant_event("ROOT");

  // toggle even odd and wake up engine waiting on vsync.
  // TODO: we could play with moving this earlier, right after the final bucket renderer.
  //       it breaks the VIF-interrupt profiling though.
  {
    prof().instant_event("engine-notify");
    std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
    g_gfx_data->frame_idx++;
    g_gfx_data->sync_cv.notify_all();
  }

  // reboot whole game, if requested
  if (g_gfx_data->debug_gui.want_reboot_in_debug) {
    g_gfx_data->debug_gui.want_reboot_in_debug = false;
    MasterExit = RuntimeExitStatus::RESTART_IN_DEBUG;
  }

  {
    auto p = scoped_prof("check-close-window");
    // exit if display window was closed
    if (m_should_quit) {
      std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
      MasterExit = RuntimeExitStatus::EXIT;
      g_gfx_data->sync_cv.notify_all();
    }
  }
}

/*!
 * Wait for the next vsync. Returns 0 or 1 depending on if frame is even or odd.
 * Called from the game thread, on a GOAL stack.
 */
u32 gl_vsync() {
  if (!g_gfx_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
  auto init_frame = g_gfx_data->frame_idx_of_input_data;
  g_gfx_data->sync_cv.wait(lock, [=] {
    return (MasterExit != RuntimeExitStatus::RUNNING) || g_gfx_data->frame_idx > init_frame;
  });
  return g_gfx_data->frame_idx & 1;
}

u32 gl_sync_path() {
  if (!g_gfx_data) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(g_gfx_data->sync_mutex);
  g_gfx_data->last_engine_time = g_gfx_data->engine_timer.getSeconds();
  if (!g_gfx_data->has_data_to_render) {
    return 0;
  }
  g_gfx_data->sync_cv.wait(lock, [=] { return !g_gfx_data->has_data_to_render; });
  return 0;
}

/*!
 * Send DMA to the renderer.
 * Called from the game thread, on a GOAL stack.
 */
void gl_send_chain(const void* data, u32 offset) {
  if (g_gfx_data) {
    std::unique_lock<std::mutex> lock(g_gfx_data->dma_mutex);
    if (g_gfx_data->has_data_to_render) {
      lg::error(
          "Gfx::send_chain called when the graphics renderer has pending data. Was this called "
          "multiple times per frame?");
      return;
    }

    // we copy the dma data and give a copy of it to the render.
    // the copy has a few advantages:
    // - if the game code has a bug and corrupts the DMA buffer, the renderer won't see it.
    // - the copied DMA is much smaller than the entire game memory, so it can be dumped to a
    // file
    //    separate of the entire RAM.
    // - it verifies the DMA data is valid early on.
    // but it may also be pretty expensive. Both the renderer and the game wait on this to
    // complete.

    // The renderers should just operate on DMA chains, so eliminating this step in the future
    // may be easy.

    g_gfx_data->dma_copier.set_input_data(data, offset, run_dma_copy);

    g_gfx_data->has_data_to_render = true;
    g_gfx_data->dma_cv.notify_all();
  }
}

/*!
 * Upload texture outside of main DMA chain.
 * We trust the game to not remove textures that are currently being used, but if the game is messed
 * up, there is a possible race to updating this texture.
 */
void gl_texture_upload_now(const u8* tpage, int mode, u32 s7_ptr) {
  // block
  if (g_gfx_data) {
    // just pass it to the texture pool.
    // the texture pool will take care of locking.
    // we don't want to lock here for the entire duration of the conversion.
    g_gfx_data->texture_pool->handle_upload_now(tpage, mode, g_ee_main_mem, s7_ptr, false);
  }
}

/*!
 * Handle a local->local texture copy. The texture pool can just update texture pointers.
 * This is called from the main thread and the texture pool itself will handle locking.
 */
void gl_texture_relocate(u32 destination, u32 source, u32 format) {
  if (g_gfx_data) {
    g_gfx_data->texture_pool->relocate(destination, source, format);
  }
}

void gl_set_levels(const std::vector<std::string>& levels) {
  g_gfx_data->loader->set_want_levels(levels);
}

void gl_set_active_levels(const std::vector<std::string>& levels) {
  g_gfx_data->loader->set_active_levels(levels);
}

void gl_force_reload_all() {
  g_gfx_data->loader->request_reload_all();
}

void gl_force_reload_level(const std::string& name) {
  g_gfx_data->loader->request_reload_level(name);
}

void gl_force_reload_common() {
  g_gfx_data->loader->request_reload_common();
}

void gl_set_pmode_alp(float val) {
  g_gfx_data->pmode_alp = val;
}

const GfxRendererModule gRendererOpenGL = {
    gl_init,                 // init
    gl_make_display,         // make_display
    gl_exit,                 // exit
    gl_vsync,                // vsync
    gl_sync_path,            // sync_path
    gl_send_chain,           // send_chain
    gl_texture_upload_now,   // texture_upload_now
    gl_texture_relocate,     // texture_relocate
    gl_set_levels,           // set_levels
    gl_set_active_levels,    // set_active_levels
    gl_force_reload_all,     // force_reload_all
    gl_force_reload_level,   // force_reload_level
    gl_force_reload_common,  // force_reload_common
    gl_set_pmode_alp,        // set_pmode_alp
    GfxPipeline::OpenGL,     // pipeline
    "OpenGL 4.3"             // name
};
