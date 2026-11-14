# FenrirUI's tests, headless (the spec's seam 1): an App with FenrirAssets and FenrirUI and no window (but for the text
# entry's clipboard test, which makes a hidden one and is skipped without a display), each root drawing
# into a capturing target whose draw lists the tests read (seam 2). The layout tests run Yoga's generated conformance
# fixtures, converted to element trees with inline styles by fixtures/convert-yoga-fixtures.py.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirUITests PublicHeaders.cpp Layout.cpp Tree.cpp Painting.cpp StylesheetParser.cpp Cascade.cpp
                             Input.cpp Focus.cpp Listeners.cpp Motion.cpp Text.cpp Controls.cpp DefaultStylesheet.cpp
                             TextEntry.cpp Gamepad.cpp Markup.cpp StyleFiles.cpp
                             UITestApp.hpp
                             fixtures/YogaFixtures.inc)
target_link_libraries(FenrirUITests PRIVATE FenrirUI FenrirAssets FenrirApp FenrirWindow Catch2::Catch2WithMain)
# Text.cpp registers a family from HarfBuzz's test fonts, where FenrirText's submodule keeps them.
target_compile_definitions(FenrirUITests
                           PRIVATE FENRIR_TEXT_HARFBUZZ_TEST="${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirText/libs/harfbuzz/test")
if(MSVC)
    # The fixtures' table is one long initializer.
    target_compile_options(FenrirUITests PRIVATE /bigobj)
endif()
add_test(NAME FenrirUI COMMAND FenrirUITests)

# Image roots on the renderer's null backend (seam 4), read back through the renderer tests' RendererProbe and
# RendererTestApp, as FenrirPainterTests reads its pipelines.
set(renderer_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirRenderer/Tests")
set(renderer_src "${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirRenderer/src")
add_executable(FenrirUIRendererTests ImageRoots.cpp)
target_include_directories(FenrirUIRendererTests PRIVATE "${renderer_tests}" "${renderer_src}")
target_link_libraries(FenrirUIRendererTests PRIVATE FenrirUI FenrirRenderer FenrirRendererCore Catch2::Catch2WithMain)
# RendererTestApp's material shader sampling one texture, which its helpers name.
fenrir_add_shaders(FenrirUIRendererTests PROGRAM SampledPicture VERTEX "${renderer_src}/../Shaders/Builtin/mesh.vert"
                   FRAGMENT "${renderer_tests}/fixtures/sampled.frag" EMBED)
add_test(NAME FenrirUI-Renderer COMMAND FenrirUIRendererTests)
