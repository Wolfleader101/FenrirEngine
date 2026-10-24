# FenrirAssets's third-party libraries, built static and linked PRIVATE behind its importer seam (ADR-0021), with the
# overrides of the spec's "Build" section. Everything set here stays in this directory, so none of it reaches the rest of
# the engine.
#
# stb (the libs/stb submodule, a pinned commit) is header-only: stb_image is compiled once, here, as FenrirStbImage.
# It reads PNG, JPEG and HDR and nothing else, from memory (FenrirAssets reads the files). cgltf (libs/cgltf, tag v1.15)
# reads glTF and GLB, and ufbx (libs/ufbx, tag v0.23.1) reads FBX and OBJ (ADR-0031); each is compiled once, here.
# Debug and Release are supported; RelWithDebInfo and MinSizeRel build them too but are not mapped to anything of
# theirs.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: these libraries' warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(FenrirStbImage STATIC StbImage.c)
target_include_directories(FenrirStbImage SYSTEM PUBLIC stb)
target_compile_definitions(FenrirStbImage PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG STBI_ONLY_HDR STBI_NO_STDIO)
# FenrirAssets links each library PRIVATE, so no other target sees it; to FenrirAssets's own sources its header is a
# system header.
set_target_properties(FenrirStbImage PROPERTIES SYSTEM TRUE)

add_library(FenrirCgltf STATIC Cgltf.c)
target_include_directories(FenrirCgltf SYSTEM PUBLIC cgltf)
set_target_properties(FenrirCgltf PROPERTIES SYSTEM TRUE)

add_library(FenrirUfbx STATIC ufbx/ufbx.c)
target_include_directories(FenrirUfbx SYSTEM PUBLIC ufbx)
set_target_properties(FenrirUfbx PROPERTIES SYSTEM TRUE)
if(MSVC)
    # ufbx.c is one very large translation unit.
    target_compile_options(FenrirUfbx PRIVATE /bigobj)
endif()
