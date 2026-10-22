#pragma once

/*!
 * @file sdl2_compat.h
 * There's no open SDL3 backend for Switch homebrew (see the development notes) - only devkitPro's
 * community SDL2 fork. SDL3 renamed a lot of SDL2's constants (GameController -> Gamepad,
 * dropped the "GameController" prefix on some functions, added an SDL_ prefix to keymod flags,
 * event type names, etc). This aliases the SDL3 names this codebase was written against onto
 * their SDL2 equivalents, so call sites that only reference *names* (not different function
 * signatures or return-type semantics - those still need real #ifdef branches) don't need to
 * change at all.
 *
 * Include this after <SDL.h> anywhere the SDL3 names below are used and __SWITCH__ is defined.
 */

#ifdef __SWITCH__

// Keymod flags: SDL3 added an SDL_ prefix.
#define SDL_KMOD_SHIFT KMOD_SHIFT
#define SDL_KMOD_ALT KMOD_ALT
#define SDL_KMOD_CTRL KMOD_CTRL
#define SDL_KMOD_GUI KMOD_GUI

// Gamepad axes: SDL3 renamed the GameController API to Gamepad.
#define SDL_GAMEPAD_AXIS_LEFTX SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_RIGHTX SDL_CONTROLLER_AXIS_RIGHTX
#define SDL_GAMEPAD_AXIS_RIGHTY SDL_CONTROLLER_AXIS_RIGHTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT

// Gamepad buttons: SDL3 also switched from Xbox-letter naming (A/B/X/Y) to positional naming
// (SOUTH/EAST/WEST/NORTH). The physical button is the same either way (SDL2's controller
// database already treats A as south/B as east/X as west/Y as north for every layout).
#define SDL_GAMEPAD_BUTTON_SOUTH SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_WEST SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_LEFT_STICK SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_BACK SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_START SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT SDL_CONTROLLER_BUTTON_DPAD_RIGHT

// Event types: SDL3 renamed these with an SDL_EVENT_ prefix.
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define SDL_EVENT_GAMEPAD_ADDED SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED SDL_CONTROLLERDEVICEREMOVED
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_EVENT_GAMEPAD_BUTTON_UP SDL_CONTROLLERBUTTONUP
#define SDL_EVENT_GAMEPAD_AXIS_MOTION SDL_CONTROLLERAXISMOTION
#define SDL_EVENT_JOYSTICK_ADDED SDL_JOYDEVICEADDED
#define SDL_EVENT_JOYSTICK_REMOVED SDL_JOYDEVICEREMOVED

// Gamepad type/misc: SDL3 renamed the GameController type to Gamepad here too.
#define SDL_Gamepad SDL_GameController
#define SDL_GamepadType SDL_GameControllerType
#define SDL_OpenGamepad SDL_GameControllerOpen
#define SDL_CloseGamepad SDL_GameControllerClose
#define SDL_IsGamepad SDL_IsGameController
#define SDL_GetGamepadName SDL_GameControllerName
#define SDL_GetGamepadType SDL_GameControllerGetType
#define SDL_GetGamepadJoystick SDL_GameControllerGetJoystick
#define SDL_RumbleGamepad SDL_GameControllerRumble
#define SDL_RumbleGamepadTriggers SDL_GameControllerRumbleTriggers
#define SDL_SetGamepadLED SDL_GameControllerSetLED
#define SDL_SendGamepadEffect SDL_GameControllerSendEffect
#define SDL_AddGamepadMappingsFromFile SDL_GameControllerAddMappingsFromFile

// Letter keycodes: SDL3 made these uppercase (SDLK_A), SDL2 keeps them lowercase (SDLK_a) since
// SDLK_* values there are just the ASCII code of the character.
#define SDLK_A SDLK_a
#define SDLK_B SDLK_b
#define SDLK_C SDLK_c
#define SDLK_D SDLK_d
#define SDLK_E SDLK_e
#define SDLK_F SDLK_f
#define SDLK_G SDLK_g
#define SDLK_H SDLK_h
#define SDLK_I SDLK_i
#define SDLK_J SDLK_j
#define SDLK_K SDLK_k
#define SDLK_L SDLK_l
#define SDLK_M SDLK_m
#define SDLK_N SDLK_n
#define SDLK_O SDLK_o
#define SDLK_P SDLK_p
#define SDLK_Q SDLK_q
#define SDLK_R SDLK_r
#define SDLK_S SDLK_s
#define SDLK_T SDLK_t
#define SDLK_U SDLK_u
#define SDLK_V SDLK_v
#define SDLK_W SDLK_w
#define SDLK_X SDLK_x
#define SDLK_Y SDLK_y
#define SDLK_Z SDLK_z

// Punctuation: SDL3 renamed this one.
#define SDLK_APOSTROPHE SDLK_QUOTE

// More gamepad/joystick renames (SDL3 GameController -> Gamepad, plus a few Joystick renames).
#define SDL_GAMEPAD_AXIS_INVALID SDL_CONTROLLER_AXIS_INVALID
#define SDL_GAMEPAD_AXIS_COUNT SDL_CONTROLLER_AXIS_MAX
#define SDL_GAMEPAD_BUTTON_INVALID SDL_CONTROLLER_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_COUNT SDL_CONTROLLER_BUTTON_MAX
#define SDL_GAMEPAD_TYPE_PS3 SDL_CONTROLLER_TYPE_PS3
#define SDL_GAMEPAD_TYPE_PS5 SDL_CONTROLLER_TYPE_PS5
#define SDL_EVENT_JOYSTICK_AXIS_MOTION SDL_JOYAXISMOTION
#define SDL_GetNumJoystickAxes SDL_JoystickNumAxes
#define SDL_GetJoystickAxis SDL_JoystickGetAxis
#define SDL_GetJoystickID SDL_JoystickInstanceID
#define SDL_GetJoystickGUID SDL_JoystickGetGUID
#define SDL_GUIDToString SDL_JoystickGetGUIDString

// SDL_Event union member names: SDL3 renamed the GameController axis/button event structs
// (event.gaxis/event.gbutton -> SDL2's event.caxis/event.cbutton). Safe as bare macros - grepped
// the whole codebase, "gaxis"/"gbutton" aren't used as identifiers for anything else.
#define gaxis caxis
#define gbutton cbutton

// SDL3 renamed the SDL_BUTTON(x) bitmask macro to SDL_BUTTON_MASK(x).
#define SDL_BUTTON_MASK(x) SDL_BUTTON(x)

// SDL3 added a modstate out-param to SDL_GetScancodeFromKey (layout-independent lookup); SDL2's
// version only takes the keycode. Overloaded (not a macro) so call sites passing both args still
// compile unchanged - the modstate just isn't refined by this call on Switch.
inline SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key, SDL_Keymod* /*modstate*/) {
  return SDL_GetScancodeFromKey(key);
}

// NOTE: SDL3's property system (SDL_GetGamepadProperties/SDL_GetBooleanProperty/
// SDL_PROP_GAMEPAD_CAP_*) has no SDL2 equivalent - SDL2 exposes the same three capability
// queries as direct per-controller bool-returning functions instead
// (SDL_GameControllerHasLED/HasRumble/HasRumbleTriggers). Since the property lookup is decoupled
// from the controller handle at the SDL3 call site, this can't be bridged with a generic macro -
// handled with a real __SWITCH__ branch at the one call site in game_controller.cpp instead.

#endif  // __SWITCH__
