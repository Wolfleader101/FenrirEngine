# SPIKE (prototype/fenrir-renderer-spike, throwaway): glslang and SPIRV-Reflect for FenrirShaderCompiler (ADR-0018).
# Built only into the build-time tool, never linked into the engine.

set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
add_subdirectory(glslang)

add_library(SpirvReflect STATIC SPIRV-Reflect/spirv_reflect.c SPIRV-Reflect/spirv_reflect.h)
target_include_directories(SpirvReflect SYSTEM PUBLIC SPIRV-Reflect)

set_target_properties(glslang PROPERTIES SYSTEM TRUE)
