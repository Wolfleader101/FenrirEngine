# FenrirWindow's tests: a hidden main window driven one frame at a time (spec: "Testing Decisions"). Tests tagged
# [display] need SDL's video to start, so without a display they report skipped (Catch2's SKIP, exit code 4), never
# passed.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

# The tests' one way inside FenrirWindow: it pushes SDL events and resizes windows behind a neutral interface. It is a
# library of its own so that SDL's include path reaches it and not the test executable, which PublicHeaders.cpp checks.
add_library(FenrirWindowProbe STATIC WindowProbe.hpp WindowProbe.cpp)
target_link_libraries(FenrirWindowProbe PUBLIC FenrirWindow PRIVATE SDL3::SDL3-static)
target_include_directories(FenrirWindowProbe PRIVATE ../src)

add_executable(FenrirWindowTests PublicHeaders.cpp WindowTestApp.hpp Installs.cpp Windows.cpp Services.cpp
                                 Surfaces.cpp WindowInput.cpp)
target_link_libraries(FenrirWindowTests PRIVATE FenrirWindowProbe Catch2::Catch2WithMain)

# Everything that needs no display.
add_test(NAME FenrirWindow COMMAND FenrirWindowTests "~[display]~[no-video]")
# Everything that needs a hidden window: skipped without a display.
add_test(NAME FenrirWindow-Display COMMAND FenrirWindowTests "[display]")
set_tests_properties(FenrirWindow-Display PROPERTIES SKIP_RETURN_CODE 4 TIMEOUT 120)
# Install when SDL's video cannot start: a video driver that does not exist stands in for a missing display.
add_test(NAME FenrirWindow-NoVideo COMMAND FenrirWindowTests "[no-video]")
set_tests_properties(FenrirWindow-NoVideo PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=no-such-driver")
