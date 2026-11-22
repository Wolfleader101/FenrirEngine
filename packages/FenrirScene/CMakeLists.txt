add_library(FenrirScene STATIC
    include/FenrirScene/Scene.hpp

    src/Scene.cpp
)

target_link_libraries(FenrirScene PUBLIC FenrirECS)
# A scene resource type's named doors hang on the type registry (ADR-0074): Scene.hpp defines the hook a generated
# registration calls for each scene resource, and Scene's named doors read what it hangs. Nothing else needs it.
target_link_libraries(FenrirScene PUBLIC FenrirTypes)

target_include_directories(FenrirScene PUBLIC include)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON, which needs the libs/catch2 submodule at the repository root (git submodule update --init).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
