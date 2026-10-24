# FenrirDraw2D's tests: its plain data, headless, and the check that it reaches no GPU, window or renderer code.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirDraw2DTests PublicHeaders.cpp DrawList2D.cpp)
target_link_libraries(FenrirDraw2DTests PRIVATE FenrirDraw2D Catch2::Catch2WithMain)
add_test(NAME FenrirDraw2D COMMAND FenrirDraw2DTests)
