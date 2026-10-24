add_library(FenrirMath STATIC
    src/Math.cpp
    include/FenrirMath/Colour.hpp
    include/FenrirMath/Math.hpp
    include/FenrirMath/Math_fwd.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirMath PUBLIC glm)

target_include_directories(FenrirMath PUBLIC include)

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirMath.api.json.
fenrir_describe_api(FenrirMath api/FenrirMath.api.toml)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
