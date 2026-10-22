# FenrirMath's own tests. No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirMathTests Colour.cpp)
target_link_libraries(FenrirMathTests PRIVATE FenrirMath Catch2::Catch2WithMain)
add_test(NAME FenrirMath COMMAND FenrirMathTests)
