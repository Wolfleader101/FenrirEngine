# FenrirCore's own tests, through its public headers only. No App, no file, no clock: the reload watcher is told the
# time by the test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirCoreTests PublicHeaders.cpp ReloadWatcherTests.cpp)
target_link_libraries(FenrirCoreTests PRIVATE FenrirCore Catch2::Catch2WithMain)
add_test(NAME FenrirCore COMMAND FenrirCoreTests --order decl)
