# FenrirPainter's tests. FenrirPainterTests: the painter by what it appends to a 2D draw list (the spec's seam 2), and
# its shader and pipeline on the renderer's null backend (seam 4), read back through the renderer tests' RendererProbe
# and RendererTestApp. FenrirPainterGlTests: builtin:painter's pixels on OpenGL, painted into an image 2D target and
# read back through the renderer GL tests' GlScene and GlProbe; without a display or an OpenGL 4.6 core context every
# test reports skipped. Labelled gl: Windows CI runs the null tests only.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(renderer_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirRenderer/Tests")
set(renderer_src "${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirRenderer/src")

add_executable(FenrirPainterTests PainterTest.hpp Shapes.cpp Paints.cpp ClipsAndBatching.cpp Pipelines.cpp Paths.cpp
                                  Strokes.cpp
                                  Groups.cpp Text.cpp AntiAliasing.cpp)
# Text.cpp lays paragraphs out through FenrirText's test fixture, in HarfBuzz's test fonts where its submodule keeps
# them.
set(text_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirText/Tests")
target_include_directories(FenrirPainterTests PRIVATE "${renderer_tests}" "${renderer_src}" "${text_tests}")
target_compile_definitions(FenrirPainterTests
                           PRIVATE FENRIR_TEXT_HARFBUZZ_TEST="${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirText/libs/harfbuzz/test")
target_link_libraries(FenrirPainterTests PRIVATE FenrirPainter FenrirRenderer FenrirRendererCore Catch2::Catch2WithMain)
# RendererTestApp's material shader sampling one texture, which its helpers name.
fenrir_add_shaders(FenrirPainterTests PROGRAM SampledPicture VERTEX "${renderer_src}/../Shaders/Builtin/mesh.vert"
                   FRAGMENT "${renderer_tests}/fixtures/sampled.frag" EMBED)
add_test(NAME FenrirPainter COMMAND FenrirPainterTests)

add_executable(FenrirPainterGlTests OpenGL/PainterPixels.cpp)
target_include_directories(FenrirPainterGlTests PRIVATE "${renderer_tests}" "${renderer_tests}/OpenGL" "${renderer_src}")
target_link_libraries(FenrirPainterGlTests PRIVATE FenrirPainter FenrirRenderer FenrirRendererCore FenrirRendererOpenGL
                                                   FenrirWindow FenrirApp FenrirRendererGoldenImages
                                                   Catch2::Catch2WithMain)
# RendererTestApp's material shader, which GlScene's helpers name.
fenrir_add_shaders(FenrirPainterGlTests PROGRAM SampledPicture VERTEX "${renderer_src}/../Shaders/Builtin/mesh.vert"
                   FRAGMENT "${renderer_tests}/fixtures/sampled.frag" EMBED)
add_test(NAME FenrirPainter-GL COMMAND FenrirPainterGlTests)
set_tests_properties(FenrirPainter-GL PROPERTIES SKIP_RETURN_CODE 4 TIMEOUT 300 LABELS gl)
