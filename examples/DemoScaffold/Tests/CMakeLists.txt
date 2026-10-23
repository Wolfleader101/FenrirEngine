# The demo scaffold's tests: a demo app on the null backend with no window, its input fed through the input feed and
# the App driven one frame at a time, or run as a smoke run.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirDemoScaffoldTests DemoTestApp.hpp SmokeRuns.cpp SceneList.cpp FreeFlyCamera.cpp)
target_link_libraries(FenrirDemoScaffoldTests PRIVATE FenrirDemoScaffold FenrirDebugDraw Catch2::Catch2WithMain)
add_test(NAME FenrirDemoScaffold COMMAND FenrirDemoScaffoldTests)
