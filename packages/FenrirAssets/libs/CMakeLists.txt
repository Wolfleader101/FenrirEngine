# FenrirAssets's third-party libraries, built static and linked PRIVATE behind its importer seam (ADR-0021), with the
# overrides of the spec's "Build" section. Everything set here stays in this directory, so none of it reaches the rest of
# the engine.
#
# stb (the libs/stb submodule, a pinned commit) is header-only: stb_image is compiled once, here, as FenrirStbImage.
# It reads PNG, JPEG and HDR and nothing else, from memory (FenrirAssets reads the files). Debug and Release are
# supported; RelWithDebInfo and MinSizeRel build it too but are not mapped to anything of stb's.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: stb's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(FenrirStbImage STATIC StbImage.c)
target_include_directories(FenrirStbImage SYSTEM PUBLIC stb)
target_compile_definitions(FenrirStbImage PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG STBI_ONLY_HDR STBI_NO_STDIO)
# FenrirAssets links it PRIVATE, so no other target sees it; to FenrirAssets's own sources its header is a system header.
set_target_properties(FenrirStbImage PROPERTIES SYSTEM TRUE)
