# FenrirApp's own tests: the App driven one frame at a time with a chosen delta. No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirAppTests Frames.cpp)
target_link_libraries(FenrirAppTests PRIVATE FenrirApp Catch2::Catch2WithMain)
add_test(NAME FenrirApp COMMAND FenrirAppTests --order decl)
