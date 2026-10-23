# FenrirStbTruetype: stb_truetype, which rasterises the debug font into its signed-distance-field atlas (ADR-0025). The
# header comes from FenrirAssets's stb submodule (packages/FenrirAssets/libs/stb, a pinned commit) through this
# target's own include of it, so FenrirAssets exposes nothing for it. Only the renderer core links it, PRIVATE. Built
# with the overrides of the spec's "Build" section; everything set here stays in this directory.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: stb's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(FenrirStbTruetype STATIC StbTruetype.c)
target_include_directories(FenrirStbTruetype SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirAssets/libs/stb")
set_target_properties(FenrirStbTruetype PROPERTIES SYSTEM TRUE)
