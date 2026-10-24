# FenrirDraw2D: 2D draw data as plain data, with no GPU code, knowing no renderer, UI or language (ADR-0027). UI
# libraries and a future painter fill DrawList2Ds; FenrirRenderer draws them. Header-only, depending on FenrirAssets
# (asset handles, samplers, colour spaces) and FenrirMath only. Built with FENRIR_ENABLE_ASSETS.
add_library(FenrirDraw2D INTERFACE)
target_include_directories(FenrirDraw2D INTERFACE include)
target_link_libraries(FenrirDraw2D INTERFACE FenrirAssets FenrirMath)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
