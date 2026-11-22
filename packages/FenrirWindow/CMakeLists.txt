# FenrirWindow: the main window, its services and graphics surface, and the input it feeds to FenrirInput, over SDL3
# (ADR-0017, ADR-0020, ADR-0022). Only src/ includes SDL, and SDL is linked PRIVATE, so neither its headers nor its
# compile definitions reach FenrirWindow's users. Built with FENRIR_ENABLE_RENDERER.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
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

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirWindow.api.json,
# which FenrirWindowScripting's binding binds.
fenrir_describe_api(FenrirWindow api/FenrirWindow.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirWindow/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirWindow api/FenrirWindow.api.toml)
target_sources(FenrirWindow PRIVATE include/FenrirWindow/Generated/FenrirWindowTypes.hpp
                                    src/Generated/FenrirWindowTypes.cpp)
target_link_libraries(FenrirWindow PUBLIC FenrirTypes)
