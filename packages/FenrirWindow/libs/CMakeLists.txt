# SDL3, FenrirWindow's windowing library (ADR-0020), pinned to a 3.4.x release tag and built static. Everything set
# here stays in this directory, so none of it reaches the rest of the engine (spec: "Build").
#
# Debug and Release are supported; RelWithDebInfo and MinSizeRel build, but are not mapped to anything of SDL's.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: SDL's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_WERROR OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
set(SDL_INSTALL OFF)
set(SDL_UNINSTALL OFF)

# Built: video (windows, the GL context, Vulkan surface support, clipboard, keyboard, mouse, text input and IME),
# events, joystick, gamepad, haptic, hidapi and sensor. Off: the subsystems ADR-0020 leaves off. SDL 3.4 has no switch
# for its process API, so it is compiled but never called, and the static link leaves it out. SDL_main is not used:
# FenrirWindow defines SDL_MAIN_HANDLED and calls SDL_SetMainReady.
set(SDL_AUDIO OFF)
set(SDL_GPU OFF)
set(SDL_RENDER OFF)
set(SDL_CAMERA OFF)
set(SDL_DIALOG OFF)
set(SDL_TRAY OFF)
set(SDL_POWER OFF)

add_subdirectory(SDL)

# FenrirWindow links SDL PRIVATE, so no other target sees it; to FenrirWindow's own sources its headers are system
# headers. They reach it through SDL's header-only target, so that one is marked too.
set_target_properties(SDL3-static SDL3_Headers PROPERTIES SYSTEM TRUE)

# SDL's own warning flags still apply to its sources, and Clang on Windows warns about the CRT functions SDL uses.
# Silence them there; MSVC builds SDL without warnings, and would warn about /w overriding SDL's /W3.
if(NOT MSVC)
    target_compile_options(SDL3-static PRIVATE -w)
endif()
