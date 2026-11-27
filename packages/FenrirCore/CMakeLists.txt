add_library(FenrirCore STATIC
    src/FenrirCore.cpp
    src/ReloadWatcher.cpp
    include/FenrirCore/FenrirCore.hpp
    include/FenrirCore/ReloadWatcher.hpp
)

target_include_directories(FenrirCore PUBLIC include)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON, which needs the libs/catch2 submodule at the repository root (git submodule update --init).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
