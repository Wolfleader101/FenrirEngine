# FenrirWindow: the main window, its services and graphics surface, and the input it feeds to FenrirInput, over SDL3
# (ADR-0017, ADR-0020, ADR-0022). Only src/ includes SDL, and SDL is linked PRIVATE, so neither its headers nor its
# compile definitions reach FenrirWindow's users. Built with FENRIR_ENABLE_RENDERER.
add_subdirectory(libs)

add_library(FenrirWindow STATIC
    include/FenrirWindow/Window.hpp
    include/FenrirWindow/WindowEvents.hpp
    include/FenrirWindow/WindowTypes.hpp

    src/SdlInput.cpp
    src/SdlInput.hpp
    src/Window.cpp
    src/WindowState.hpp
)

target_link_libraries(FenrirWindow PUBLIC FenrirApp FenrirInput PRIVATE SDL3::SDL3-static)

target_include_directories(FenrirWindow PUBLIC include PRIVATE src)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
