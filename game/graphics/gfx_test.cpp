#include "gfx_test.h"

#include "game/system/hid/sdl_util.h"

#ifndef __ANDROID__
#include "third-party/glad/include/glad/glad.h"
#endif

namespace tests {
void to_json(json& j, const GPUTestOutput& obj) {
  json_serialize(success);
  json_serialize(error);
  json_serialize(errorCause);
  json_serialize_optional(gpuRendererString);
  json_serialize_optional(gpuVendorString);
}

GPUTestOutput run_gpu_test(const std::string& test_type) {
  lg::info("Running GPU Test - {}", test_type);
  GPUTestOutput output = {false, "", ""};
#ifdef __ANDROID__
  // This test creates its own standalone desktop-GL-profile window/context, unrelated to (and
  // incompatible with) the main game's real GLES renderer - not meaningful on Android as written.
  if (test_type == "opengl") {
    output = {false, "GPU test not implemented for Android (see gfx_test.cpp)", ""};
  } else {
    lg::error("Invalid GPU test type - {}", test_type);
  }
#else
  if (test_type == "opengl") {
#ifdef __SWITCH__
    // SDL2's SDL_Init returns 0 on success, negative on failure (SDL3 flipped this to bool).
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#else
    if (!SDL_Init(SDL_INIT_VIDEO)) {
#endif
      output = {false, "SDL initialization failed",
                sdl_util::log_and_return_error("SDL initialization failed")};
      return output;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
#ifdef __SWITCH__
    // SDL2's SDL_CreateWindow takes an explicit x/y position (SDL3 dropped that param).
    SDL_Window* window = SDL_CreateWindow("OpenGL Version", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 800, 600,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
#else
    SDL_Window* window =
        SDL_CreateWindow("OpenGL Version", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
#endif
    if (!window) {
      output = {false, "SDL window creation failed",
                sdl_util::log_and_return_error("SDL initialization failed")};
      SDL_Quit();
      return output;
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
      output = {false, "Required OpenGL Version is not supported",
                sdl_util::log_and_return_error("SDL initialization failed")};
    } else {
      gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
      if (!gladLoadGL()) {
        output = {false, "Unable to init GLAD", ""};
      } else {
        output = {true, "", ""};
        const auto rendererString = glGetString(GL_RENDERER);
        if (rendererString) {
          output.gpuRendererString = (const char*)rendererString;
        }
        const auto vendorString = glGetString(GL_VENDOR);
        if (vendorString) {
          output.gpuVendorString = (const char*)vendorString;
        }
      }
#ifdef __SWITCH__
      SDL_GL_DeleteContext(glContext);
#else
      SDL_GL_DestroyContext(glContext);
#endif
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
  } else {
    lg::error("Invalid GPU test type - {}", test_type);
  }
#endif
  return output;
}
};  // namespace tests
