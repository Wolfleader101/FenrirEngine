# FenrirAssets's tests: its public interface, headless (the spec's seam 4). The image fixtures are small files in
# fixtures/, read where they are.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirAssetsTests PublicHeaders.cpp Owners.cpp Revisions.cpp Loading.cpp Images.cpp ShaderContainers.cpp
                                 AssetsTest.hpp)
target_link_libraries(FenrirAssetsTests PRIVATE FenrirAssets Catch2::Catch2WithMain)
target_compile_definitions(FenrirAssetsTests PRIVATE FENRIR_ASSETS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME FenrirAssets COMMAND FenrirAssetsTests)

# With the renderer on, FenrirShaderCompiler builds a fixture shader from GLSL for the parameter-table test.
if(FENRIR_ENABLE_RENDERER)
    target_sources(FenrirAssetsTests PRIVATE ShaderParameters.cpp)
    fenrir_add_shaders(FenrirAssetsTests PROGRAM Parameters VERTEX fixtures/shaders/parameters.vert
                       FRAGMENT fixtures/shaders/parameters.frag EMBED)
endif()
