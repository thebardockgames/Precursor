# Set a more convenient ARM flag
# NOTE: devkitA64's toolchain reports CMAKE_SYSTEM_PROCESSOR as "aarch64" (standard GNU triple
# naming), not Apple's "arm64" - both need to match here.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ARM64_ARCH TRUE)
    message(STATUS "ARM64 architecture detected")
else()
    set(ARM64_ARCH FALSE)
    message(STATUS "Non-ARM64 architecture detected")
endif()

if(ARM64_ARCH)
    # Add your ARM64-specific configuration or build options here
    set(OG_ASM_FUNCS_FILE kernel/asm_funcs_arm64.s)
    enable_language(ASM)
    set(CMAKE_ASM_SOURCE_FILE_EXTENSIONS ${CMAKE_ASM_SOURCE_FILE_EXTENSIONS} s)
    if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        # -arch is a Clang/Apple-only flag - GNU as (devkitA64) doesn't understand it and needs
        # no explicit arch flag here since the cross-compiler's own target triple already implies
        # aarch64.
        set_source_files_properties(${OG_ASM_FUNCS_FILE} PROPERTIES COMPILE_FLAGS "-g")
    else()
        set_source_files_properties(${OG_ASM_FUNCS_FILE} PROPERTIES COMPILE_FLAGS "-arch arm64 -g")
    endif()
else()
    set(OG_ASM_FUNCS_FILE kernel/asm_funcs_x86_64.asm)
    enable_language(ASM_NASM)
    set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS ${CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS} asm)
    set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
    set_source_files_properties(${OG_ASM_FUNCS_FILE} PROPERTIES COMPILE_FLAGS "-g")
endif()

set(RUNTIME_SOURCE
        external/discord_jak1.cpp
        external/discord_jak2.cpp
        external/discord_jak3.cpp
        external/discord_jakx.cpp
        external/discord.cpp
        graphics/display.cpp
        graphics/gfx_test.cpp
        graphics/gfx.cpp
        graphics/jak2_texture_remap.cpp
        graphics/jak3_texture_remap.cpp
        graphics/jakx_texture_remap.cpp
        graphics/screenshot.cpp
        graphics/opengl_renderer/background/background_common.cpp
        graphics/opengl_renderer/background/Hfrag.cpp
        graphics/opengl_renderer/background/Shrub.cpp
        graphics/opengl_renderer/background/TFragment.cpp
        graphics/opengl_renderer/background/Tie3.cpp
        graphics/opengl_renderer/BlitDisplays.cpp
        graphics/opengl_renderer/BucketRenderer.cpp
        graphics/opengl_renderer/CollideMeshRenderer.cpp
        graphics/opengl_renderer/debug_gui.cpp
        graphics/opengl_renderer/DepthCue.cpp
        graphics/opengl_renderer/DirectRenderer.cpp
        graphics/opengl_renderer/DirectRenderer2.cpp
        graphics/opengl_renderer/dma_helpers.cpp
        graphics/opengl_renderer/EyeRenderer.cpp
        graphics/opengl_renderer/foreground/Generic2_Build.cpp
        graphics/opengl_renderer/foreground/Generic2_DMA.cpp
        graphics/opengl_renderer/foreground/Generic2_OpenGL.cpp
        graphics/opengl_renderer/foreground/Generic2.cpp
        graphics/opengl_renderer/foreground/Generic2BucketRenderer.cpp
        graphics/opengl_renderer/foreground/Merc2.cpp
        graphics/opengl_renderer/foreground/Merc2BucketRenderer.cpp
        graphics/opengl_renderer/foreground/Shadow2.cpp
        graphics/opengl_renderer/loader/Loader.cpp
        graphics/opengl_renderer/loader/LoaderStages.cpp
        graphics/opengl_renderer/ocean/CommonOceanRenderer.cpp
        graphics/opengl_renderer/ocean/OceanEnvmap.cpp
        graphics/opengl_renderer/ocean/OceanMid_PS2.cpp
        graphics/opengl_renderer/ocean/OceanMid.cpp
        graphics/opengl_renderer/ocean/OceanMidAndFar.cpp
        graphics/opengl_renderer/ocean/OceanNear_PS2.cpp
        graphics/opengl_renderer/ocean/OceanNear.cpp
        graphics/opengl_renderer/ocean/OceanTexture_PC.cpp
        graphics/opengl_renderer/ocean/OceanTexture.cpp
        graphics/opengl_renderer/opengl_utils.cpp
        graphics/opengl_renderer/OpenGLRenderer.cpp
        graphics/opengl_renderer/Profiler.cpp
        graphics/opengl_renderer/ProgressRenderer.cpp
        graphics/opengl_renderer/Shader.cpp
        graphics/opengl_renderer/Shadow_PS2.cpp
        graphics/opengl_renderer/ShadowRenderer.cpp
        graphics/opengl_renderer/SkyBlendCPU.cpp
        graphics/opengl_renderer/SkyBlendGPU.cpp
        graphics/opengl_renderer/SkyRenderer.cpp
        graphics/opengl_renderer/SlowTimeEffect.cpp
        graphics/opengl_renderer/sprite/GlowRenderer.cpp
        graphics/opengl_renderer/sprite/Sprite3_Distort.cpp
        graphics/opengl_renderer/sprite/Sprite3_Glow.cpp
        graphics/opengl_renderer/sprite/Sprite3.cpp
        graphics/opengl_renderer/TextureAnimator.cpp
        graphics/opengl_renderer/TextureAnimatorDefs.cpp
        graphics/opengl_renderer/TextureUploadHandler.cpp
        graphics/opengl_renderer/VisDataHandler.cpp
        graphics/opengl_renderer/Warp.cpp
        graphics/pipelines/opengl.cpp
        graphics/sceGraphicsInterface.cpp
        graphics/texture/jak1_tpage_dir.cpp
        graphics/texture/jak2_tpage_dir.cpp
        graphics/texture/jak3_tpage_dir.cpp
        graphics/texture/TextureConverter.cpp
        graphics/texture/TexturePool.cpp
        ${OG_ASM_FUNCS_FILE}
        kernel/common/fileio.cpp
        kernel/common/kboot.cpp
        kernel/common/kdgo.cpp
        kernel/common/kdsnetm.cpp
        kernel/common/klink.cpp
        kernel/common/klisten.cpp
        kernel/common/kmachine.cpp
        kernel/common/kmalloc.cpp
        kernel/common/kmemcard.cpp
        kernel/common/kprint.cpp
        kernel/common/kscheme.cpp
        kernel/common/ksocket.cpp
        kernel/common/ksound.cpp
        kernel/jak1/fileio.cpp
        kernel/jak1/kboot.cpp
        kernel/jak1/kdgo.cpp
        kernel/jak1/klink.cpp
        kernel/jak1/klisten.cpp
        kernel/jak1/kmachine.cpp
        kernel/jak1/kprint.cpp
        kernel/jak1/kscheme.cpp
        kernel/jak1/ksound.cpp
        kernel/jak2/fileio.cpp
        kernel/jak2/kboot.cpp
        kernel/jak2/kdgo.cpp
        kernel/jak2/klink.cpp
        kernel/jak2/klisten.cpp
        kernel/jak2/kmachine.cpp
        kernel/jak2/kmachine_extras.cpp
        kernel/jak2/kmalloc.cpp
        kernel/jak2/kprint.cpp
        kernel/jak2/kscheme.cpp
        kernel/jak2/ksound.cpp
        kernel/jak3/fileio.cpp
        kernel/jak3/kboot.cpp
        kernel/jak3/kdgo.cpp
        kernel/jak3/kdsnetm.cpp
        kernel/jak3/klink.cpp
        kernel/jak3/klisten.cpp
        kernel/jak3/kmachine.cpp
        kernel/jak3/kmachine_extras.cpp
        kernel/jak3/kmalloc.cpp
        kernel/jak3/kmemcard.cpp
        kernel/jak3/kprint.cpp
        kernel/jak3/kscheme.cpp
        kernel/jak3/ksocket.cpp
        kernel/jak3/ksound.cpp
        kernel/jakx/fileio.cpp
        kernel/jakx/kboot.cpp
        kernel/jakx/kdgo.cpp
        kernel/jakx/kdsnetm.cpp
        kernel/jakx/klink.cpp
        kernel/jakx/klisten.cpp
        kernel/jakx/kmachine.cpp
        kernel/jakx/kmachine_extras.cpp
        kernel/jakx/kmalloc.cpp
        kernel/jakx/kmemcard.cpp
        kernel/jakx/kprint.cpp
        kernel/jakx/kscheme.cpp
        kernel/jakx/ksocket.cpp
        kernel/jakx/ksound.cpp
        mips2c/jak1_functions/bones.cpp
        mips2c/jak1_functions/collide_cache.cpp
        mips2c/jak1_functions/collide_edge_grab.cpp
        mips2c/jak1_functions/collide_func.cpp
        mips2c/jak1_functions/collide_mesh.cpp
        mips2c/jak1_functions/collide_probe.cpp
        mips2c/jak1_functions/draw_string.cpp
        mips2c/jak1_functions/generic_effect.cpp
        mips2c/jak1_functions/generic_effect2.cpp
        mips2c/jak1_functions/generic_merc.cpp
        mips2c/jak1_functions/generic_tie.cpp
        mips2c/jak1_functions/joint.cpp
        mips2c/jak1_functions/merc_blend_shape.cpp
        mips2c/jak1_functions/ocean_vu0.cpp
        mips2c/jak1_functions/ocean.cpp
        mips2c/jak1_functions/ripple.cpp
        mips2c/jak1_functions/shadow.cpp
        mips2c/jak1_functions/sky_tng.cpp
        mips2c/jak1_functions/sparticle_launcher.cpp
        mips2c/jak1_functions/sparticle.cpp
        mips2c/jak1_functions/test_func.cpp
        mips2c/jak1_functions/texture.cpp
        mips2c/jak1_functions/tfrag.cpp
        mips2c/jak1_functions/tie_methods.cpp
        mips2c/jak1_functions/time_of_day.cpp
        mips2c/jak2_functions/bones.cpp
        mips2c/jak2_functions/collide_cache.cpp
        mips2c/jak2_functions/collide_edge_grab.cpp
        mips2c/jak2_functions/collide_func.cpp
        mips2c/jak2_functions/collide_hash.cpp
        mips2c/jak2_functions/collide_mesh.cpp
        mips2c/jak2_functions/debug.cpp
        mips2c/jak2_functions/font.cpp
        mips2c/jak2_functions/foreground.cpp
        mips2c/jak2_functions/generic_effect.cpp
        mips2c/jak2_functions/generic_merc.cpp
        mips2c/jak2_functions/joint.cpp
        mips2c/jak2_functions/lights.cpp
        mips2c/jak2_functions/merc_blend_shape.cpp
        mips2c/jak2_functions/nav_control.cpp
        mips2c/jak2_functions/nav_mesh.cpp
        mips2c/jak2_functions/ocean_vu0.cpp
        mips2c/jak2_functions/ocean.cpp
        mips2c/jak2_functions/ripple.cpp
        mips2c/jak2_functions/shadow.cpp
        mips2c/jak2_functions/sky.cpp
        mips2c/jak2_functions/sparticle_launcher.cpp
        mips2c/jak2_functions/sparticle.cpp
        mips2c/jak2_functions/spatial_hash.cpp
        mips2c/jak2_functions/squid.cpp
        mips2c/jak2_functions/texture.cpp
        mips2c/jak2_functions/vortex.cpp
        mips2c/jak3_functions/collide_func.cpp
        mips2c/jak3_functions/debug.cpp
        mips2c/jak3_functions/font.cpp
        mips2c/jak3_functions/foreground.cpp
        mips2c/jak3_functions/generic_effect.cpp
        mips2c/jak3_functions/joint.cpp
        mips2c/jak3_functions/lights.cpp
        mips2c/jak3_functions/sky.cpp
        mips2c/jak3_functions/texture.cpp
        mips2c/jak3_functions/particle_curves.cpp
        mips2c/jak3_functions/cloth.cpp
        mips2c/jak3_functions/collide_cache.cpp
        mips2c/jak3_functions/collide_hash.cpp
        mips2c/jak3_functions/collide_edge_grab.cpp
        mips2c/jak3_functions/collide_mesh.cpp
        mips2c/jak3_functions/spatial_hash.cpp
        mips2c/jak3_functions/sparticle.cpp
        mips2c/jak3_functions/sparticle_launcher.cpp
        mips2c/jak3_functions/nav_control.cpp
        mips2c/jak3_functions/nav_engine.cpp
        mips2c/jak3_functions/merc_blend_shape.cpp
        mips2c/jak3_functions/wvehicle_part.cpp
        mips2c/jak3_functions/ripple.cpp
        mips2c/jak3_functions/shadow.cpp
        mips2c/jak3_functions/ocean.cpp
        mips2c/jak3_functions/ocean_vu0.cpp
        mips2c/jak3_functions/generic_merc.cpp
        mips2c/mips2c_table.cpp
        overlord/common/dma.cpp
        overlord/common/fake_iso.cpp
        overlord/common/iso_api.cpp
        overlord/common/iso.cpp
        overlord/common/isocommon.cpp
        overlord/common/989_plugins/plugin_strv.cpp
        overlord/common/overlord.cpp
        overlord/common/sbank.cpp
        overlord/common/soundcommon.cpp
        overlord/common/srpc.cpp
        overlord/common/ssound.cpp
        overlord/jak1/dma.cpp
        overlord/jak1/fake_iso.cpp
        overlord/jak1/iso_api.cpp
        overlord/jak1/iso_queue.cpp
        overlord/jak1/iso.cpp
        overlord/jak1/isocommon.cpp
        overlord/jak1/overlord.cpp
        overlord/jak1/ramdisk.cpp
        overlord/jak1/srpc.cpp
        overlord/jak1/ssound.cpp
        overlord/jak1/stream.cpp
        overlord/jak2/dma.cpp
        overlord/jak2/iso_api.cpp
        overlord/jak2/iso_cd.cpp
        overlord/jak2/iso_queue.cpp
        overlord/jak2/iso.cpp
        overlord/jak2/list.cpp
        overlord/jak2/overlord.cpp
        overlord/jak2/pages.cpp
        overlord/jak2/989_plugins/plugin_strv.cpp
        overlord/jak2/spustreams.cpp
        overlord/jak2/srpc.cpp
        overlord/jak2/ssound.cpp
        overlord/jak2/stream.cpp
        overlord/jak2/streamlfo.cpp
        overlord/jak2/streamlist.cpp
        overlord/jak2/vag.cpp
        overlord/jak3/overlord.cpp
        overlord/jak3/pagemanager.cpp
        overlord/jak3/iso_cd.cpp
        overlord/jak3/dma.cpp
        overlord/jak3/iso.cpp
        overlord/jak3/iso_queue.cpp
        overlord/jak3/srpc.cpp
        overlord/jak3/vag.cpp
        overlord/jak3/ssound.cpp
        overlord/jak3/iso_api.cpp
        overlord/jak3/spustreams.cpp
        overlord/jak3/list.cpp
        overlord/jak3/vblank_handler.cpp
        overlord/jak3/dvd_driver.cpp
        overlord/jak3/basefile.cpp
        overlord/jak3/basefilesystem.cpp
        overlord/jak3/ramdisk.cpp
        overlord/jak3/isocommon.cpp
        overlord/jak3/init.cpp
        overlord/jak3/stream.cpp
        overlord/jak3/sbank.cpp
        overlord/jak3/soundcommon.cpp
        overlord/jak3/streamlist.cpp
        overlord/jak3/989_plugins/plugin_strv.cpp
        overlord/jakx/overlord.cpp
        overlord/jakx/pagemanager.cpp
        overlord/jakx/iso_cd.cpp
        overlord/jakx/dma.cpp
        overlord/jakx/iso.cpp
        overlord/jakx/iso_queue.cpp
        overlord/jakx/srpc.cpp
        overlord/jakx/vag.cpp
        overlord/jakx/ssound.cpp
        overlord/jakx/iso_api.cpp
        overlord/jakx/spustreams.cpp
        overlord/jakx/list.cpp
        overlord/jakx/vblank_handler.cpp
        overlord/jakx/dvd_driver.cpp
        overlord/jakx/basefile.cpp
        overlord/jakx/basefilesystem.cpp
        overlord/jakx/ramdisk.cpp
        overlord/jakx/isocommon.cpp
        overlord/jakx/init.cpp
        overlord/jakx/stream.cpp
        overlord/jakx/sbank.cpp
        overlord/jakx/soundcommon.cpp
        overlord/jakx/streamlist.cpp
        runtime.cpp
        sce/deci2.cpp
        sce/iop.cpp
        sce/libcdvd_ee.cpp
        sce/libdma.cpp
        sce/libgraph.cpp
        sce/libpad.cpp
        sce/libscf.cpp
        sce/sif_ee_memcard.cpp
        sce/sif_ee.cpp
        sce/stubs.cpp
        settings/settings.cpp
        system/background_worker.cpp
        system/Deci2Server.cpp
        system/hid/devices/dualsense_effects.cpp
        system/hid/devices/game_controller.cpp
        system/hid/devices/keyboard.cpp
        system/hid/devices/mouse.cpp
        system/hid/display_manager.cpp
        system/hid/input_bindings.cpp
        system/hid/input_manager.cpp
        system/hid/android_touch_controls.cpp
        system/hid/sdl_util.cpp
        system/IOP_Kernel.cpp
        system/iop_thread.cpp
        system/SystemThread.cpp
        tools/filter_menu/filter_menu.cpp
        tools/subtitle_editor/subtitle_editor_db.cpp
        tools/subtitle_editor/subtitle_editor_repl_client.cpp
        tools/subtitle_editor/subtitle_editor.cpp
        )

add_subdirectory(sound)

# we build the runtime as a static library.
if(ANDROID)
    # No glad on Android - real GLES entry points are directly linkable symbols (see
    # game/graphics/pipelines/opengl.h), so there's no loader to build/link at all.
    add_library(runtime STATIC ${RUNTIME_SOURCE})
else()
    add_library(runtime STATIC ${RUNTIME_SOURCE} "../third-party/glad/src/glad.c")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # No open SDL3 backend for Switch (see the development notes) - uses devkitPro's SDL2 fork
    # instead. No Discord client or libcurl portlib on Switch either - both are stubbed out at
    # their one/two call sites (game/external/discord.cpp, game/system/background_worker.cpp)
    # rather than linked.
    target_link_libraries(runtime common fmt SDL2::SDL2-static imgui sound stb_image libco)
elseif(ANDROID)
    # SDL3 has real, official Android support (unlike Switch) - no SDL2 downgrade needed here.
    # No Discord client to reach from a phone and no libcurl build set up for Android yet - both
    # stubbed at their call sites, same as Switch (see the development notes).
    #
    # Two different Android consumers of this same game/CMakeLists.txt need different SDL3
    # linkage: scripts/android-port/cmake-poc builds a plain `adb shell`-launched executable and
    # forces the static SDL3::SDL3-static target (one self-contained binary, no .so to push
    # separately); scripts/android-port/apk builds a real installable APK, where the native code
    # is loaded via JNI as a shared library and SDL3 itself must be a separate loadable
    # libSDL3.so (SDLActivity.java's loadLibraries() loads "SDL3" before "main") - that project
    # only defines the SDL3::SDL3 (shared) target. Pick whichever one the including project
    # actually built rather than hardcoding one, so both stay working.
    if(TARGET SDL3::SDL3-static)
        target_link_libraries(runtime common fmt SDL3::SDL3-static imgui sound stb_image libco)
    else()
        target_link_libraries(runtime common fmt SDL3::SDL3 imgui sound stb_image libco)
    endif()
else()
    target_link_libraries(runtime common fmt SDL3::SDL3 imgui discord-rpc sound stb_image libco libcurl)
endif()
if(WIN32)
    target_link_libraries(runtime mman)
elseif(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # newlib's dl (dlopen) doesn't exist on Switch; pthread does (newlib provides libpthread.a).
    target_link_libraries(runtime pthread)
elseif(ANDROID)
    # bionic has never had a separate libpthread - those symbols live directly in libc (there's
    # no "-lpthread" to find), unlike glibc/newlib. dlopen() is real and does need linking though.
    # imgui's OpenGL3 backend auto-selects IMGUI_IMPL_OPENGL_ES3 for __ANDROID__ (see
    # imgui_impl_opengl3.h) and links directly against real GLES3 entry points like
    # glGenVertexArrays/glBindVertexArray - those live in libGLESv3's stub, a separate one from
    # the libGLESv1_CM/libGLESv2 SDL's own CMakeLists.txt already links.
    target_link_libraries(runtime dl GLESv3)
else()
    target_link_libraries(runtime pthread dl)
endif()

add_executable(gk main.cpp)
target_link_libraries(gk runtime)
if(WIN32)
    set_target_properties(gk PROPERTIES VS_DPI_AWARE "PerMonitor")
endif()

# Real installable Android APK entry point (scripts/android-port/apk only - OFF by default,
# doesn't affect the PC build or the headless scripts/android-port/cmake-poc executable). Android
# JNI loads native code as a shared library, not an executable - SDLActivity.java's
# getMainSharedObject()/getMainFunction() expect a "libmain.so" exporting a real "SDL_main" symbol
# (SDL3/SDL_main.h #define's main to SDL_main when included, so the same main.cpp works
# unchanged - see main.cpp's own matching OPENGOAL_ANDROID_APK_BUILD guard around that include).
option(OPENGOAL_ANDROID_APK_BUILD "Build the 'main' shared library for a real Android APK (scripts/android-port/apk) instead of/alongside the headless 'gk' executable" OFF)
if(OPENGOAL_ANDROID_APK_BUILD)
    add_library(main SHARED main.cpp)
    target_compile_definitions(main PRIVATE OPENGOAL_ANDROID_APK_BUILD)
    target_link_libraries(main runtime)
endif()
