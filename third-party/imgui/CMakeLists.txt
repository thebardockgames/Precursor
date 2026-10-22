if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # No open SDL3 backend exists for Switch homebrew (see the development notes) - only devkitPro's
    # community SDL2 fork. imgui_impl_sdl2.{h,cpp} vendored to match this project's imgui version
    # (1.92.6) from https://github.com/ocornut/imgui/tree/v1.92.6/backends.
    add_library(imgui
            imgui.cpp
            imgui_draw.cpp
            imgui_tables.cpp
            imgui_widgets.cpp
            imgui_impl_sdl2.cpp
            imgui_impl_opengl3.cpp
            imgui_stdlib.cpp
            imgui_style.cpp)
    target_link_libraries(imgui SDL2::SDL2)
else()
    add_library(imgui
            imgui.cpp
            imgui_draw.cpp
            imgui_tables.cpp
            imgui_widgets.cpp
            imgui_impl_sdl3.cpp
            imgui_impl_opengl3.cpp
            imgui_stdlib.cpp
            imgui_style.cpp)
    target_link_libraries(imgui SDL3::SDL3)
endif()
