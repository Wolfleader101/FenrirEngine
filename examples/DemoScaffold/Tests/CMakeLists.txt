# The demo scaffold's tests: a demo app on the null backend with no window, its input fed through the input feed and
# the App driven one frame at a time, or run as a smoke run.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirDemoScaffoldTests DemoTestApp.hpp SmokeRuns.cpp SceneList.cpp FreeFlyCamera.cpp ImGuiOverlay.cpp)
target_link_libraries(FenrirDemoScaffoldTests PRIVATE FenrirDemoScaffold FenrirDebugDraw Catch2::Catch2WithMain)
add_test(NAME FenrirDemoScaffold COMMAND FenrirDemoScaffoldTests)
# With scripting built the scaffold links it, so the tests need what it loads at run time beside them.
if(FENRIR_ENABLE_SCRIPTING)
    fenrir_deploy_scripting(FenrirDemoScaffoldTests EXTENSIONS Input Window Assets Renderer DebugDraw)
endif()
