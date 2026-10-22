set(SOUND_SOURCES
  989snd/player.cpp
  989snd/midi_handler.cpp
  989snd/ame_handler.cpp
  989snd/blocksound_handler.cpp
  989snd/musicbank.cpp
  989snd/sfxblock.cpp
  989snd/sfxgrain.cpp
  989snd/plugin.cpp
  989snd/loader.cpp
  989snd/vagvoice.cpp
  989snd/lfo.cpp
  989snd/util.cpp
  common/synth.cpp
  common/voice.cpp
  common/envelope.cpp
  sndshim.cpp
  sdshim.cpp
  )

add_library(sound STATIC ${SOUND_SOURCES})
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # No cubeb backend for Switch - game/sound/989snd/player.{h,cpp} talks to libnx's audout
    # service directly instead. See the development notes.
    target_link_libraries(sound PRIVATE fmt common)
else()
    # cubeb has real Android backends (OpenSL ES / AudioTrack), auto-detected via its own
    # check_include_files() calls - no Switch-style bypass needed here.
    target_link_libraries(sound PRIVATE fmt cubeb common)
endif()

# sndplay is a standalone dev/test tool, not needed on a Switch or Android player build.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" AND NOT ANDROID)
    add_executable(sndplay 989snd/sndplay.cpp)
    if(WIN32)
        target_link_libraries(sndplay PRIVATE sound cubeb)
    elseif(APPLE)
        target_link_libraries(sndplay PRIVATE sound cubeb)
    else()
        target_link_libraries(sndplay PRIVATE sound cubeb stdc++fs)
    endif()
endif()

if (NOT WIN32)
    target_compile_options(sound
            PRIVATE
            -Wno-unknown-warning-option
            -Wno-unused-private-field
            -Wno-unused-parameter
            -Wno-shadow
            )
endif()
