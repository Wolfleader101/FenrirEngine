# UIDemo's tests: the app's HUD set up as main sets it up, on the null backend with no window, driven a frame at a
# time, its root drawing into a capturing target. The scaffold's asset root is `assets` beside the executable, so the
# shared demo assets and the app's own are copied there, as fenrir_add_demo_app copies them beside the app.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(UIDemoTests DemoApp.hpp HudSetup.cpp MenuSetup.cpp ScreenSetup.cpp)
target_link_libraries(UIDemoTests PRIVATE UIDemoCore FenrirLogger Catch2::Catch2WithMain)
add_custom_command(TARGET UIDemoTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FENRIR_DEMO_SHARED_ASSETS}" "$<TARGET_FILE_DIR:UIDemoTests>/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
            "$<TARGET_FILE_DIR:UIDemoTests>/assets"
    COMMENT "Copying the demo assets into UIDemoTests' asset root"
    VERBATIM
)
add_test(NAME UIDemo COMMAND UIDemoTests)
# With scripting built the scaffold links it, so the tests need what it loads at run time beside them.
if(FENRIR_ENABLE_SCRIPTING)
    fenrir_deploy_scripting(UIDemoTests EXTENSIONS Input Window Assets Renderer DebugDraw)
endif()
