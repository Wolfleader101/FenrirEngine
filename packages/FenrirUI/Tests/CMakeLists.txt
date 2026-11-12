# FenrirUI's tests, headless (the spec's seam 1): an App with FenrirAssets and FenrirUI and no window, each root drawing
# into a capturing target whose draw lists the tests read (seam 2). The layout tests run Yoga's generated conformance
# fixtures, converted to element trees with inline styles by fixtures/convert-yoga-fixtures.py.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirUITests PublicHeaders.cpp Layout.cpp Tree.cpp Painting.cpp UITestApp.hpp
                             fixtures/YogaFixtures.inc)
target_link_libraries(FenrirUITests PRIVATE FenrirUI FenrirAssets FenrirApp Catch2::Catch2WithMain)
if(MSVC)
    # The fixtures' table is one long initializer.
    target_compile_options(FenrirUITests PRIVATE /bigobj)
endif()
add_test(NAME FenrirUI COMMAND FenrirUITests)
