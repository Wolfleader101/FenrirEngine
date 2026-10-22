# FenrirInput: device-neutral input types, the Keyboard, Mouse and Gamepads app resources, input events and the input
# feed (ADR-0017). Links no third-party library and is always built, like FenrirTime.
add_library(FenrirInput STATIC
    src/Devices.cpp
    src/Input.cpp
    src/InputDispatch.hpp
    src/InputFeed.cpp
    include/FenrirInput/Devices.hpp
    include/FenrirInput/Input.hpp
    include/FenrirInput/InputEvents.hpp
    include/FenrirInput/InputFeed.hpp
    include/FenrirInput/InputTypes.hpp
)

target_link_libraries(FenrirInput PUBLIC FenrirApp FenrirMath)

target_include_directories(FenrirInput PUBLIC include)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
