# glslang and SPIRV-Reflect, FenrirShaderCompiler's libraries (ADR-0018), pinned tags built static, with the overrides
# of the spec's "Build" section. Only the build-time tool links them, PRIVATE; no engine target ever does. Everything
# set here stays in this directory, so none of it reaches the rest of the engine.
#
# Debug and Release are supported; RelWithDebInfo and MinSizeRel build, but are not mapped to anything of theirs.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: their warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# glslang: the GLSL front end and SPIR-V back end only. No HLSL front end, no optimizer (so no SPIRV-Tools), no
# binaries (whose build would need Python), no tests and no install.
set(ENABLE_HLSL OFF)
set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES OFF)
set(ENABLE_GLSLANG_JS OFF)
set(GLSLANG_TESTS OFF)
set(GLSLANG_ENABLE_INSTALL OFF)
set(BUILD_EXTERNAL OFF)
set(BUILD_WERROR OFF)
set(ENABLE_PCH OFF)
add_subdirectory(glslang)

# SPIRV-Reflect: its static library only, without its executable, tests or install.
set(SPIRV_REFLECT_EXECUTABLE OFF)
set(SPIRV_REFLECT_STATIC_LIB ON)
set(SPIRV_REFLECT_BUILD_TESTS OFF)
set(SPIRV_REFLECT_INSTALL OFF)
add_subdirectory(SPIRV-Reflect)

# FenrirShaderCompiler links them PRIVATE, so no other target sees them; to the tool's own sources their headers are
# system headers.
set_target_properties(glslang glslang-default-resource-limits spirv-reflect-static PROPERTIES SYSTEM TRUE)

# Their own warning flags still apply to their sources, and Clang on Windows warns about the CRT functions they use.
# Silence them there; MSVC would warn about /w overriding their own levels.
if(NOT MSVC)
    target_compile_options(glslang PRIVATE -w)
    target_compile_options(glslang-default-resource-limits PRIVATE -w)
    target_compile_options(spirv-reflect-static PRIVATE -w)
endif()
