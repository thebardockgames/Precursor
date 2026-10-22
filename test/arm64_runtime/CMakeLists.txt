cmake_minimum_required(VERSION 3.16)
project(opengoal_arm64_runtime_tests LANGUAGES CXX ASM)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "These runtime tests require the Android arm64-v8a NDK toolchain")
endif()

get_filename_component(JAK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(arm64-runtime-tests
  main.cpp
  probes.s
  ${JAK_ROOT}/game/kernel/asm_funcs_arm64.s)
target_include_directories(arm64-runtime-tests PRIVATE ${JAK_ROOT})
target_compile_features(arm64-runtime-tests PRIVATE cxx_std_20)

enable_testing()
foreach(test_case direct array pp mips2c goal goal8 stack nothing zero format cache
                  goal-vectors goal8-vectors stack-vectors)
  add_test(NAME arm64-runtime-${test_case} COMMAND arm64-runtime-tests ${test_case})
endforeach()
