# Dear ImGui (the libs/imgui submodule, tag v1.92.9b), linked only by the demo scaffold for its ImGuiOverlay (ADR-0027),
# with the overrides of the spec's "Build" section. Everything set here stays in this directory, so none of it reaches
# the rest of the engine. Only ImGui's core is built: the overlay is its platform and renderer backend, so none of
# ImGui's own backends are. Debug and Release are supported; RelWithDebInfo and MinSizeRel build it too but are not
# mapped to anything of its.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: ImGui's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(FenrirDemoImGui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
target_include_directories(FenrirDemoImGui SYSTEM PUBLIC imgui)
# The overlay gives ImGui the clipboard and the IME through FenrirWindow, so ImGui's own Win32 versions, which would
# need user32 and imm32, are left out, as is opening links in the OS shell, which nothing uses.
target_compile_definitions(FenrirDemoImGui PUBLIC IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
                                                  IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
                                                  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
# To the scaffold's and the demo apps' sources ImGui's headers are system headers.
set_target_properties(FenrirDemoImGui PROPERTIES SYSTEM TRUE)
