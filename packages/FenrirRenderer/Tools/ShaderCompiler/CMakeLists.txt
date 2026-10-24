# FenrirShaderCompiler: the build-time tool of ADR-0018, run by fenrir_add_shaders (cmake/FenrirShaders.cmake). It
# compiles a GLSL 460 program's stages and variants to SPIR-V, writes the GL copy, reflects them, checks the parameter
# defaults, and writes one shader container. It links glslang and SPIRV-Reflect PRIVATE, and nothing in the engine links
# it: engine targets see only the containers it writes, through FenrirShaderContainer.
add_subdirectory(libs)

add_executable(FenrirShaderCompiler
    src/main.cpp
    src/Arguments.cpp
    src/Arguments.hpp
    src/Diagnostics.hpp
    src/GlRewrite.cpp
    src/GlRewrite.hpp
    src/Pragmas.cpp
    src/Pragmas.hpp
    src/Program.cpp
    src/Program.hpp
    src/Reflection.cpp
    src/Reflection.hpp
    src/StageCompiler.cpp
    src/StageCompiler.hpp
)

target_link_libraries(FenrirShaderCompiler PRIVATE FenrirShaderContainer glslang::glslang
                                                   glslang::glslang-default-resource-limits spirv-reflect-static)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
