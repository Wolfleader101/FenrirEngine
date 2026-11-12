# FenrirText's tests: its public interface, headless (the spec's seam 3). Paragraphs are laid out in HarfBuzz's own
# test fonts, read where the submodule keeps them, and in the embedded default face.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirTextTests PublicHeaders.cpp Atlas.cpp Fallback.cpp Paragraphs.cpp Segments.cpp TextTest.hpp)
target_link_libraries(FenrirTextTests PRIVATE FenrirText Catch2::Catch2WithMain)
target_compile_definitions(FenrirTextTests
                           PRIVATE FENRIR_TEXT_HARFBUZZ_TEST="${CMAKE_CURRENT_SOURCE_DIR}/../libs/harfbuzz/test")
add_test(NAME FenrirText COMMAND FenrirTextTests)
