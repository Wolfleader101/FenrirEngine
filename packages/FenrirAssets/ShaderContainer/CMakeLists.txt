# FenrirShaderContainer: the shader container format (ADR-0018), with its reader and writer and the GL flattening
# formula. It depends on nothing, so FenrirAssets (ShaderData's parameter table), the renderer core and the build-time
# FenrirShaderCompiler all link the one definition of the format.
add_library(FenrirShaderContainer STATIC
    include/FenrirAssets/ShaderContainer.hpp
    include/FenrirAssets/ShaderGlBindings.hpp

    src/ShaderContainer.cpp
)

target_include_directories(FenrirShaderContainer PUBLIC include)
