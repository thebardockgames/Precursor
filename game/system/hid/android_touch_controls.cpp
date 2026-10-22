#include "android_touch_controls.h"

#ifdef __ANDROID__
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

#include <jni.h>

#include "common/log/log.h"
#include "third-party/SDL/include/SDL3/SDL.h"

namespace {
// Java only publishes state. SDL consumes it in its normal joystick update, so
// pause/resume and multi-touch cannot race SDL device lifetime. (AI-assisted)
std::mutex state_mutex;
std::array<float, 4> stick_axes{};
uint32_t pressed_buttons = 0;
SDL_Joystick* touch_joystick = nullptr;
SDL_JoystickID touch_id = 0;
std::atomic<uint64_t> presented_frames{0};
std::atomic<uint64_t> game_frames{0};
std::atomic<int> render_scale{100};
constexpr uint32_t kLeftTrigger = 1u << 15;
constexpr uint32_t kRightTrigger = 1u << 16;
constexpr int kButtonCount = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;

void SDLCALL update(void*) {
  if (!touch_joystick) {
    return;
  }
  std::array<float, 4> axes;
  uint32_t buttons;
  {
    std::lock_guard<std::mutex> lock(state_mutex);
    axes = stick_axes;
    buttons = pressed_buttons;
  }
  for (int i = 0; i < 4; ++i) {
    SDL_SetJoystickVirtualAxis(touch_joystick, i, static_cast<Sint16>(axes[i] * 32767.f));
  }
  SDL_SetJoystickVirtualAxis(touch_joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                            (buttons & kLeftTrigger) ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
  SDL_SetJoystickVirtualAxis(touch_joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                            (buttons & kRightTrigger) ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
  for (int i = 0; i < kButtonCount; ++i) {
    SDL_SetJoystickVirtualButton(touch_joystick, i, (buttons & (1u << i)) != 0);
  }
}
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_opengoalandroid_jak1_TouchControlsView_nativeSetState(
    JNIEnv*, jclass, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jint buttons) {
  std::lock_guard<std::mutex> lock(state_mutex);
  stick_axes = {lx, ly, rx, ry};
  for (auto& axis : stick_axes) {
    axis = std::isfinite(axis) ? std::clamp(axis, -1.f, 1.f) : 0.f;
  }
  pressed_buttons = static_cast<uint32_t>(buttons);
}

namespace android_touch_controls {
void frame_presented() {
  presented_frames.fetch_add(1, std::memory_order_relaxed);
}

void game_frame_rendered() {
  game_frames.fetch_add(1, std::memory_order_relaxed);
}

void initialize() {
  if (touch_id) {
    return;
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.name = "OpenGOAL Touch Controls";
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = kButtonCount;
  desc.Update = update;
  touch_id = SDL_AttachVirtualJoystick(&desc);
  if (touch_id) {
    touch_joystick = SDL_OpenJoystick(touch_id);
  }
  if (!touch_joystick) {
    lg::error("Could not initialize Android touch controller: {}", SDL_GetError());
    shutdown();
    return;
  }
  update(nullptr);
  lg::info("Android touch controller ready (two sticks, D-pad, PS2 buttons)");
}

void shutdown() {
  if (touch_joystick) {
    SDL_CloseJoystick(touch_joystick);
    touch_joystick = nullptr;
  }
  if (touch_id) {
    SDL_DetachVirtualJoystick(touch_id);
    touch_id = 0;
  }
  std::lock_guard<std::mutex> lock(state_mutex);
  stick_axes.fill(0.f);
  pressed_buttons = 0;
}

int render_scale_percent() {
  return render_scale.load(std::memory_order_relaxed);
}
}  // namespace android_touch_controls

// Set by Jak1Activity from the game's Graphics options before the game starts. (AI-assisted)
extern "C" JNIEXPORT void JNICALL
Java_com_opengoalandroid_jak1_Jak1Activity_nativeSetRenderScale(JNIEnv*, jclass, jint percent) {
  render_scale.store(std::clamp(static_cast<int>(percent), 25, 100), std::memory_order_relaxed);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_opengoalandroid_jak1_TouchControlsView_nativeGetFrameCount(JNIEnv*, jclass) {
  return static_cast<jlong>(presented_frames.load(std::memory_order_relaxed));
}

// Jak1Activity hides its loading screen once the game has drawn real frames. (AI-assisted)
extern "C" JNIEXPORT jlong JNICALL
Java_com_opengoalandroid_jak1_Jak1Activity_nativeGetGameFrameCount(JNIEnv*, jclass) {
  return static_cast<jlong>(game_frames.load(std::memory_order_relaxed));
}
#endif
